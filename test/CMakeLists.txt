find_package(ament_cmake_gtest REQUIRED)
find_package(test_msgs REQUIRED)

add_subdirectory(benchmark)

ament_add_gtest(
  test_executors
  executors/test_executors.cpp
  APPEND_LIBRARY_DIRS "${append_library_dirs}"
  TIMEOUT 180)
if(TARGET test_executors)
  target_link_libraries(test_executors ${PROJECT_NAME} rcl::rcl ${test_msgs_TARGETS})
endif()

#ament_add_gtest(
  #test_executors_timer_cancel_behavior
  #executors/test_executors_timer_cancel_behavior.cpp
  #APPEND_LIBRARY_DIRS "${append_library_dirs}"
  #TIMEOUT 180)
#if(TARGET test_executors)
  #target_link_libraries(test_executors_timer_cancel_behavior ${PROJECT_NAME} ${rosgraph_msgs_TARGETS})
#endif()

ament_add_gtest(
  test_executors_callback_group_behavior
  executors/test_executors_callback_group_behavior.cpp
  APPEND_LIBRARY_DIRS "${append_library_dirs}"
  TIMEOUT 180)
if(TARGET test_executors)
  target_link_libraries(test_executors_callback_group_behavior ${PROJECT_NAME})
endif()

#ament_add_gtest(
  #test_executors_intraprocess
  #executors/test_executors_intraprocess.cpp
  #APPEND_LIBRARY_DIRS "${append_library_dirs}"
  #TIMEOUT 180)
#if(TARGET test_executors)
  #target_link_libraries(test_executors_intraprocess ${PROJECT_NAME} ${test_msgs_TARGETS})
#endif()

#ament_add_gtest(test_static_single_threaded_executor executors/test_static_single_threaded_executor.cpp
  #APPEND_LIBRARY_DIRS "${append_library_dirs}")
#ament_add_test_label(test_static_single_threaded_executor mimick)
#if(TARGET test_static_single_threaded_executor)
  #target_link_libraries(test_static_single_threaded_executor ${PROJECT_NAME} mimick ${test_msgs_TARGETS})
#endif()

#ament_add_gtest(test_multi_threaded_executor executors/test_multi_threaded_executor.cpp
  #APPEND_LIBRARY_DIRS "${append_library_dirs}")
#if(TARGET test_multi_threaded_executor)
  #target_link_libraries(test_multi_threaded_executor ${PROJECT_NAME})
#endif()

#ament_add_gtest(test_entities_collector executors/test_entities_collector.cpp
  #APPEND_LIBRARY_DIRS "${append_library_dirs}" TIMEOUT 120)
#if(TARGET test_entities_collector)
  #target_link_libraries(test_entities_collector ${PROJECT_NAME})
#endif()

#ament_add_gtest(test_executor_notify_waitable executors/test_executor_notify_waitable.cpp
  #APPEND_LIBRARY_DIRS "${append_library_dirs}" TIMEOUT 120)
#ament_add_test_label(test_executor_notify_waitable mimick)
#if(TARGET test_executor_notify_waitable)
  #target_link_libraries(test_executor_notify_waitable ${PROJECT_NAME} mimick rcpputils::rcpputils)
#endif()

#ament_add_gtest(test_events_executor executors/test_events_executor.cpp TIMEOUT 5)
#if(TARGET test_events_executor)
  #target_link_libraries(test_events_executor ${PROJECT_NAME} ${test_msgs_TARGETS})
#endif()

#ament_add_gtest(test_events_queue executors/test_events_queue.cpp
  #APPEND_LIBRARY_DIRS "${append_library_dirs}")
#if(TARGET test_events_queue)
  #target_link_libraries(test_events_queue ${PROJECT_NAME})
#endif()


#ament_add_gtest(test_executor test_executor.cpp
  #APPEND_LIBRARY_DIRS "${append_library_dirs}"
  #TIMEOUT 120)
#ament_add_test_label(test_executor mimick)
#if(TARGET test_executor)
  #target_link_libraries(test_executor ${PROJECT_NAME} mimick)
#endif()
