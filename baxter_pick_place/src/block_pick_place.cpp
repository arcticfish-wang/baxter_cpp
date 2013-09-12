/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2013, CU Boulder
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of CU Boulder nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/**
 * \brief   Simple pick place for blocks using Baxter
 * \author  Dave Coleman
 */

#include <ros/ros.h>

// MoveIt!
#include <moveit/move_group_interface/move_group.h>

// Baxter Utilities
#include <baxter_control/baxter_utilities.h>

// Grasp generation
#include <block_grasp_generator/block_grasp_generator.h>
#include <block_grasp_generator/robot_viz_tools.h> // simple tool for showing graspsp

// Baxter specific properties
#include <baxter_pick_place/baxter_data.h>
#include <baxter_pick_place/custom_environment.h>

namespace baxter_pick_place
{

static const std::string PLANNING_GROUP_NAME = "right_arm";
static const std::string RVIZ_MARKER_TOPIC = "/end_effector_marker";
static const std::string BLOCK_NAME = "block1";

struct MetaBlock
{
  std::string name;
  geometry_msgs::Pose start_pose;
  geometry_msgs::Pose goal_pose;
};

class SimplePickPlace
{
public:

  // grasp generator
  block_grasp_generator::BlockGraspGeneratorPtr block_grasp_generator_;

  block_grasp_generator::RobotVizToolsPtr rviz_tools_;

  // data for generating grasps
  block_grasp_generator::RobotGraspData grasp_data_;

  // our interface with MoveIt
  boost::scoped_ptr<move_group_interface::MoveGroup> move_group_;

  // baxter helper
  baxter_control::BaxterUtilities baxter_util_;

  // settings
  bool auto_reset_;
  int auto_reset_sec_;

  SimplePickPlace()
    : auto_reset_(true),
      auto_reset_sec_(4)
  {
    ros::NodeHandle nh;

    // Create MoveGroup for right arm
    move_group_.reset(new move_group_interface::MoveGroup(PLANNING_GROUP_NAME));
    move_group_->setPlanningTime(30.0);

    // Load the Robot Viz Tools for publishing to rviz
    rviz_tools_.reset(new block_grasp_generator::RobotVizTools( RVIZ_MARKER_TOPIC, EE_GROUP,
        PLANNING_GROUP_NAME, BASE_LINK, FLOOR_TO_BASE_HEIGHT));

    // Load grasp generator
    grasp_data_ = loadRobotGraspData(BLOCK_SIZE); // Load robot specific data

    block_grasp_generator_.reset(new block_grasp_generator::BlockGraspGenerator(rviz_tools_));

    // Let everything load
    ros::Duration(1.0).sleep();

    // Enable baxter
    if( !baxter_util_.enableBaxter() )
      return;

    // Do it.
    startRoutine();

    // Shutdown
    baxter_util_.disableBaxter();
  }

  bool startRoutine()
  {
    // Debug - calculate and output table surface dimensions
    if( false )
    {
      double y_min, y_max, x_min, x_max;
      getTableWidthRange(y_min, y_max);
      getTableDepthRange(x_min, x_max);
      ROS_INFO_STREAM_NAMED("table","Blocks width range: " << y_min << " <= y <= " << y_max);
      ROS_INFO_STREAM_NAMED("table","Blocks depth range: " << x_min << " <= x <= " << x_max);
    }

    // Create start block positions (hard coded)
    std::vector<MetaBlock> blocks;
    blocks.push_back( createStartBlock(0.55, -0.4, "Block1") );
    blocks.push_back( createStartBlock(0.65, -0.4, "Block2") );
    blocks.push_back( createStartBlock(0.75, -0.4, "Block3") );

    // The goal for each block is simply translating them on the y axis
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
      blocks[i].goal_pose = blocks[i].start_pose;
      blocks[i].goal_pose.position.y += 0.2;
    }

    // Show grasp visualizations or not
    rviz_tools_->setMuted(false);

    // Create the walls and tables
    createEnvironment(rviz_tools_);

    // --------------------------------------------------------------------------------------------------------
    // Repeat pick and place forever
    while(ros::ok())
    {
      // -------------------------------------------------------------------------------------
      // Send Baxter to neutral position
      //if( !baxter_util_.positionBaxterNeutral() )
      //  return false;

      // --------------------------------------------------------------------------------------------
      // Re-add all blocks
      for (std::size_t i = 0; i < blocks.size(); ++i)
      {
        resetBlock(blocks[i]);
      }

      // Do for all blocks
      for (std::size_t block_id = 0; block_id < blocks.size(); ++block_id)
      {
        // Pick -------------------------------------------------------------------------------------
        while(ros::ok())
        {
          ROS_INFO_STREAM_NAMED("pick_place","Picking '" << blocks[block_id].name << "'");

          // Visualize the block we are about to pick
          rviz_tools_->publishBlock( blocks[block_id].start_pose, BLOCK_SIZE, false );

          if( !pick(blocks[block_id].start_pose, blocks[block_id].name) )
          {
            ROS_ERROR_STREAM_NAMED("pick_place","Pick failed.");

            // Ask user if we should try again
            if( !promptUser() )
              exit(0);

            // Retry
            resetBlock(blocks[block_id]);
          }
          else
          {
            ROS_INFO_STREAM_NAMED("pick_place","Done with pick ---------------------------");
            break;
          }
        }

        // Place -------------------------------------------------------------------------------------
        while(ros::ok())
        {
          ROS_INFO_STREAM_NAMED("pick_place","Placing '" << blocks[block_id].name << "'");

          // Publish goal block location
          rviz_tools_->publishBlock( blocks[block_id].goal_pose, BLOCK_SIZE, true );

          if( !place(blocks[block_id].goal_pose, blocks[block_id].name) )
          {
            ROS_ERROR_STREAM_NAMED("pick_place","Place failed.");

            // Determine if the attached collision body as already been removed, in which case
            // we can ignore the failure and just resume picking
            /*
            if( !move_group_->hasAttachedObject(blocks[block_id].name) )
            {
              ROS_WARN_STREAM_NAMED("pick_place","Collision object already detached, so auto resuming pick place.");
              ROS_WARN_STREAM_NAMED("pick_place","Collision object already detached, so auto resuming pick place.");
              ROS_WARN_STREAM_NAMED("pick_place","Collision object already detached, so auto resuming pick place.");
              ROS_WARN_STREAM_NAMED("pick_place","Collision object already detached, so auto resuming pick place.");
              ROS_WARN_STREAM_NAMED("pick_place","Collision object already detached, so auto resuming pick place.");
              ROS_WARN_STREAM_NAMED("pick_place","Collision object already detached, so auto resuming pick place.");

              // Ask user if we should try again
              if( !promptUser() )
                break; // resume picking
                }
            */
  
            // Ask user if we should try again
            if( !promptUser() )
              exit(0);
          }
          else
          {
            ROS_INFO_STREAM_NAMED("pick_place","Done with place ----------------------------");
            break;
          }
        }

      }

      ROS_INFO_STREAM_NAMED("pick_place","Finished picking and placing " << blocks.size() << " blocks!");

      // Ask user if we should repeat
      if( !promptUser() )
        break;
    }

    // Move to gravity neutral position
    //if( !baxter_util_.positionBaxterNeutral() )
    //  return false;

    // Everything worked!
    return true;
  }

  void resetBlock(MetaBlock block)
  {
    // Remove attached object
    rviz_tools_->cleanupACO(block.name);

    // Remove collision object
    rviz_tools_->cleanupCO(block.name);

    // Add the collision block
    rviz_tools_->publishCollisionBlock(block.start_pose, block.name, BLOCK_SIZE);
  }

  MetaBlock createStartBlock(double x, double y, const std::string name)
  {
    MetaBlock start_block;
    start_block.name = name;

    // Position
    start_block.start_pose.position.x = x;
    start_block.start_pose.position.y = y;
    start_block.start_pose.position.z = getTableHeight(FLOOR_TO_BASE_HEIGHT);

    // Orientation
    double angle = 0; // M_PI / 1.5;
    Eigen::Quaterniond quat(Eigen::AngleAxis<double>(double(angle), Eigen::Vector3d::UnitZ()));
    start_block.start_pose.orientation.x = quat.x();
    start_block.start_pose.orientation.y = quat.y();
    start_block.start_pose.orientation.z = quat.z();
    start_block.start_pose.orientation.w = quat.w();

    return start_block;
  }

  bool pick(const geometry_msgs::Pose& block_pose, std::string block_name)
  {
    ROS_WARN_STREAM_NAMED("pick","Picking '"<< block_name << "'");

    std::vector<manipulation_msgs::Grasp> grasps;

    // Pick grasp
    block_grasp_generator_->generateGrasps( block_pose, grasp_data_, grasps );

    // Prevent collision with table
    move_group_->setSupportSurfaceName(SUPPORT_SURFACE3_NAME);

    // Allow blocks to be touched by end effector
    {
      // an optional list of obstacles that we have semantic information about and that can be touched/pushed/moved in the course of grasping
      std::vector<std::string> allowed_touch_objects;
      allowed_touch_objects.push_back("Block1");
      allowed_touch_objects.push_back("Block2");
      allowed_touch_objects.push_back("Block3");
      allowed_touch_objects.push_back("Block4");

      // Add this list to all grasps
      for (std::size_t i = 0; i < grasps.size(); ++i)
      {
        grasps[i].allowed_touch_objects = allowed_touch_objects;
      }
    }

    //ROS_INFO_STREAM_NAMED("","Grasp 0\n" << grasps[0]);

    return move_group_->pick(block_name, grasps);
  }

  bool place(const geometry_msgs::Pose& goal_block_pose, std::string block_name)
  {
    ROS_WARN_STREAM_NAMED("place","Placing '"<< block_name << "'");

    std::vector<manipulation_msgs::PlaceLocation> place_locations;
    std::vector<manipulation_msgs::Grasp> grasps;

    // Re-usable datastruct
    geometry_msgs::PoseStamped pose_stamped;
    pose_stamped.header.frame_id = BASE_LINK;
    pose_stamped.header.stamp = ros::Time::now();

    // Create 360 degrees of place location rotated around a center
    for (double angle = 0; angle < 2*M_PI; angle += M_PI/2)
    {
      ROS_INFO_STREAM_NAMED("temp","angle = " << angle);

      pose_stamped.pose = goal_block_pose;

      // Orientation
      Eigen::Quaterniond quat(Eigen::AngleAxis<double>(double(angle), Eigen::Vector3d::UnitZ()));
      pose_stamped.pose.orientation.x = quat.x();
      pose_stamped.pose.orientation.y = quat.y();
      pose_stamped.pose.orientation.z = quat.z();
      pose_stamped.pose.orientation.w = quat.w();

      // Create new place location
      manipulation_msgs::PlaceLocation place_loc;

      place_loc.place_pose = pose_stamped;

      rviz_tools_->publishBlock( place_loc.place_pose.pose, BLOCK_SIZE, true );

      // Approach
      manipulation_msgs::GripperTranslation gripper_approach;
      gripper_approach.direction.header.stamp = ros::Time::now();
      gripper_approach.desired_distance = grasp_data_.approach_retreat_desired_dist_; // The distance the origin of a robot link needs to travel
      gripper_approach.min_distance = grasp_data_.approach_retreat_min_dist_; // half of the desired? Untested.
      gripper_approach.direction.header.frame_id = grasp_data_.base_link_;
      gripper_approach.direction.vector.x = 0;
      gripper_approach.direction.vector.y = 0;
      gripper_approach.direction.vector.z = -1; // Approach direction (negative z axis)  // TODO: document this assumption
      place_loc.approach = gripper_approach;

      // Retreat
      manipulation_msgs::GripperTranslation gripper_retreat;
      gripper_retreat.direction.header.stamp = ros::Time::now();
      gripper_retreat.desired_distance = grasp_data_.approach_retreat_desired_dist_; // The distance the origin of a robot link needs to travel
      gripper_retreat.min_distance = grasp_data_.approach_retreat_min_dist_; // half of the desired? Untested.
      gripper_retreat.direction.header.frame_id = grasp_data_.base_link_;
      gripper_retreat.direction.vector.x = 0;
      gripper_retreat.direction.vector.y = 0;
      gripper_retreat.direction.vector.z = 1; // Retreat direction (pos z axis)
      place_loc.retreat = gripper_retreat;

      // Post place posture - use same as pre-grasp posture (the OPEN command)
      place_loc.post_place_posture = grasp_data_.pre_grasp_posture_;

      place_locations.push_back(place_loc);
    }

    // Prevent collision with table
    move_group_->setSupportSurfaceName(SUPPORT_SURFACE3_NAME);

    move_group_->setPlannerId("RRTConnectkConfigDefault");

    return move_group_->place(block_name, place_locations);
  }

  bool promptUser()
  {
    // Make sure ROS is still with us
    if( !ros::ok() )
      return false;

    if( auto_reset_ )
    {
      ROS_INFO_STREAM_NAMED("pick_place","Auto-retrying in " << auto_reset_sec_ << " seconds");
      ros::Duration(auto_reset_sec_).sleep();
    }
    else
    {
      ROS_INFO_STREAM_NAMED("pick_place","Retry? (y/n)");
      char input; // used for prompting yes/no
      std::cin >> input;
      if( input == 'n' )
        return false;
    }
    return true;
  }

};

} //namespace

int main(int argc, char **argv)
{
  ros::init (argc, argv, "baxter_pick_place");
  ros::AsyncSpinner spinner(1);
  spinner.start();

  // Start the pick place node
  baxter_pick_place::SimplePickPlace();

  ros::shutdown();

  return 0;
}
