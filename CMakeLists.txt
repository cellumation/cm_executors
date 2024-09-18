cmake_minimum_required(VERSION 3.20)

project(cm_executors)

find_package(Threads REQUIRED)

find_package(ament_cmake_ros REQUIRED)
find_package(ament_index_cpp REQUIRED)
#find_package(builtin_interfaces REQUIRED)
#find_package(libstatistics_collector REQUIRED)
find_package(rclcpp REQUIRED)
#find_package(rcl_interfaces REQUIRED)
#find_package(rcl_logging_interface REQUIRED)
#find_package(rcl_yaml_param_parser REQUIRED)
#find_package(rcpputils REQUIRED)
#find_package(rcutils REQUIRED)
#find_package(rmw REQUIRED)
#find_package(rosgraph_msgs REQUIRED)
#find_package(rosidl_dynamic_typesupport REQUIRED)
#find_package(rosidl_runtime_c REQUIRED)
#find_package(rosidl_runtime_cpp REQUIRED)
#find_package(rosidl_typesupport_c REQUIRED)
#find_package(rosidl_typesupport_cpp REQUIRED)
#find_package(statistics_msgs REQUIRED)
#find_package(tracetools REQUIRED)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_library(${PROJECT_NAME}
  src/events_cbg_executor.cpp
  src/global_event_id_provider.cpp
  src/FirstInFirstOutScheduler.cpp
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")
target_link_libraries(${PROJECT_NAME} PUBLIC
  rclcpp::rclcpp
)

install(
  TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

# Export old-style CMake variables
ament_export_include_directories("include/${PROJECT_NAME}")
ament_export_libraries(${PROJECT_NAME})

# Export modern CMake targets
ament_export_targets(${PROJECT_NAME})

ament_export_dependencies(
  rclcpp
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  add_subdirectory(test)
endif()

ament_package()

ament_generate_version_header(${PROJECT_NAME})
