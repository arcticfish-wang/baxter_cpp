baxter
======

Unofficial Baxter packages that add-on to the Rethink SDK. Currently it mostly contains Gazebo interface stuff, though also some MoveIt code.

## Prequisites

 * ROS Groovy or Hydro
 * [gazebo_ros_pkgs](gazebosim.org/wiki/Tutorials#ROS_Integration) installed with the latest stand-alone version of Gazebo
 * A catkinized version of baxter_msgs (Rethink will officially release this soon to baxter_common, otherwise email davetcoleman@gmail.com)

## Installation

* Create a catkin workspace and cd into it:

```
    cd ~/catkin_ws/src
```

* Checkout this repo

```
    git clone git@github.com:davetcoleman/baxter.git
```

* Also install from source a transmission version of Baxter, moveit_plugins and (optional) some grasping code

```
    git clone git@github.com:davetcoleman/baxter_common.git -b development
    git clone git@github.com:ros-planning/moveit_plugins.git
    git clone git@github.com:davetcoleman/block_grasp_generator.git
    git clone git@github.com:davetcoleman/reflexxes_controllers.git -b action_server
```

* Install dependencies

Groovy:
```
    rosdep install --from-paths . --ignore-src --rosdistro groovy -y
```

Hydro:
```
    rosdep install --from-paths . --ignore-src --rosdistro hydro -y
```

* Build

```
    cd ..
    catkin_make
```

## Run

### Launch Baxter in Gazebo:

```
roslaunch baxter_gazebo baxter_world.launch
```

### Launch Individual Generic Simulated controllers for Baxter:
Only accepts individual std_msgs/Float32 commands

```
roslaunch baxter_control baxter_individual_control.launch 
```

### Launch RQT 
to see a "dashboard" for controlling Baxter:

```
roslaunch baxter_control baxter_individual_rqt.launch 
```
This will provide you with easy ways to publish sine wave commands to the actuators, tune the PID controllers and visualize the performance.

### Run a Baxter gripper action server:
Note: requires you have a gripper modeled in the Baxter URDF. This version of the URDF is available in the [baxter_with_gripper](https://github.com/davetcoleman/baxter_common/commits/baxter_with_gripper) branch of davetcoleman/baxter_common

```
rosrun baxter_gripper_server gripper_action_server
```

## Run Experimental 
aka not working

### Launch a trajectory controller that runs a FollowJointTrajectoryAction:

First, restart everything and relaunch the baxter_world.launch file. Then:

```
roslaunch baxter_control baxter_trajectory_control.launch
roslaunch baxter_control baxter_trajectory_rqt.launch
```



## Develop and Contribute

See [Contribute](https://github.com/osrf/baxter/blob/master/CONTRIBUTING.md) page.
