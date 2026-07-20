# This work has been merged into rclcpp

# Cellumation Executors

A ROS2 package providing alternative executor implementations. This repository is kept as a historical reference.


## Features
- Compared to rclcpp::experimental::EventsExecutor
  - Multithread support
  - Support for mixing timers using different clocks (ROS_TIME/SYSTEM_TIME/STEADY_TIME)
  - Comparable performance in one thread mode
- 10-15% less CPU usage than rclcpp::SingleThreadedExecutor and rclcpp::MultiThreadedExecutor

## Known bugs
- If the process is constantly overloaded and can not process the subscriptions timers etc, over time the events bag log will grow unbounded.
