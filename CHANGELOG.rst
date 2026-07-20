^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Changelog for package cm_executors
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

0.9.1 (2025-08-13)
------------------
* feat: Added build and test action
  * Action test2
  * fix runs_on
  * use correct actionv ersion
* Test CI Build action
* chore: make cpplint happy
* chore: uncrustify
* fix: made ament_copyright happy
* fix(TimerQueue): Use specialized ClockConditionalVariable
  This fixes issues, were other calls to rclcpp::Clock::cancel_sleep_or_wait
  would wake up the timers manager. This also increases the performance,
  as the expensive registration of the shutdown callback on every sleep
  was moved to initialization / destruction of the TimerQueue.
* Update README.md
* Create README.md
* chore added license
* Fix missing test dependency
* fix: Clear timer reset callbacks, before deleting object involved in callback
* fix: Added missing mutex protection in sync_callback_groups
* fix: clear registered events on shutdown
* fix: Fixed invalid memory access on shutdown
* fix: Compile lib as shared
* fix: Don't add timer multiple times to running queue if reset was called
* fix: Handle exceptions in all threads
* feat: Added spin() with exception handler function
* chore: Removed outdated benchmark tests
* refactor: Cleaned up CMakeLists.txt
* refactor: Cleaned up RegisteredEntityCache
* fix: remove rcl polling thread
* fix: Don't wait on timer before before assiciated clock is ready
* feat: Don't expose internal data structures
  This allows us to change the executor, without breaking ABI / API
* fix: Don't start timer queue thread before initializing the class
* feat: Added PriorityScheduler
* chore: Remove log spam and outdated code
* fix(EventsCBGExecutor): Poll rcl layer for 'hidden' guard conditions
* fix(TimerManager): Use clock of registered timer for sleep
  This fixes the bug, that ROS_TIME timers were not switched to
  ros time as it is not attached to a time source.
* fix: Don't call 'call' on timer twice
* refactor: Major refactoring
* fix: implemented behaviour of spin_some of base executor
* fix: Replaced by queue event id with global event id
* fix: Made it compile
* Initial import from rclcpp
