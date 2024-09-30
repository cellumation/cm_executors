cmake_minimum_required(VERSION 3.20)

project(cm_executors)

find_package(Threads REQUIRED)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_library(${PROJECT_NAME} SHARED
  src/events_cbg_executor.cpp
  src/global_event_id_provider.cpp
  src/FirstInFirstOutScheduler.cpp
  src/PriorityScheduler.cpp
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")
target_link_libraries(${PROJECT_NAME} PUBLIC
  rclcpp::rclcpp
)

ament_export_dependencies(rclcpp)
ament_export_targets(${PROJECT_NAME} HAS_LIBRARY_TARGET)

install(
  TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)


if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  add_subdirectory(test)
endif()

ament_package()

