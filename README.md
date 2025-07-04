# Cellumation Executors
![CircleCI](https://img.shields.io/circleci/build/github/cellumation/cm_executors)
![GitHub](https://img.shields.io/github/license/cellumation/cm_executors)

A ROS2 packages providing alternative executor implementations. Currently only one executor is
provided, the EventsCBGExecutor.

## Features
- Compared to rclcpp::experimental::EventsExecutor
  - Multithread support
  - Support for mixing timers using different clocks (ROS_TIME/SYSTEM_TIME/STEADY_TIME)
  - Comparable performance in one thread mode
- 10-15% less CPU usage than rclcpp::SingleThreadedExecutor and rclcpp::MultiThreadedExecutor

## Known bugs
- If the process is constantly overloaded and can not process the subscriptions timers etc, over time the events bag log will grow unbounded.

## Usage
```cpp
#include <cm_executors/events_cbg_executor.hpp>

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  // multithreaded mode, use at least 2 thread or as many threads as processor cores detected
  int numThreads = 0;

  // Single threaded mode, has better latencies because of less context switches
  //int numThreads = 1;

  rclcpp::executors::EventsCBGExecutor executor(rclcpp::ExecutorOptions(), numThreads);

  // add nodes etc

  executor.spin()
}
```
