find_package(ament_cmake_google_benchmark REQUIRED)
find_package(performance_test_fixture REQUIRED)

# These benchmarks are only being created and run for the default RMW
# implementation. We are looking to test the performance of the ROS 2 code, not
# the underlying middleware.
add_performance_test(benchmark_executor benchmark_executor.cpp)
if(TARGET benchmark_executor)
  target_link_libraries(benchmark_executor ${PROJECT_NAME} rclcpp::rclcpp ${test_msgs_TARGETS})
endif()

