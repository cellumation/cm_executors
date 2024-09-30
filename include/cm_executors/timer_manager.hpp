#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <vector>
#include <map>

#include <rcl/timer.h>
#include <rclcpp/timer.hpp>

#include <inttypes.h>

namespace rclcpp::executors
{

/**
 * @brief A class for managing a queue of timers
 *
 * This class holds a queue of timers of one type (RCL_ROS_TIME, RCL_SYSTEM_TIME or RCL_STEADY_TIME).
 * The queue itself manages an internal map of the timers, orders by the next time a timer will be
 * ready. Each time a timer is ready, a callback will be called from the internal thread.
 */
class TimerQueue
{
  struct TimerData
  {
    std::shared_ptr<const rcl_timer_t> rcl_ref;
    rclcpp::Clock::SharedPtr clock;
    bool in_running_list = false;
    std::function<void(const std::function<void()> executed_cb)> timer_ready_callback;
  };

  class GetClockHelper : public rclcpp::TimerBase
  {
  public:
    static rclcpp::Clock::SharedPtr get_clock(const rclcpp::TimerBase &timer)
    {
      // SUPER ugly hack, but we need the correct clock
      return static_cast<const GetClockHelper *>(&timer)->clock_;
    }
  };

public:
  TimerQueue(rcl_clock_type_t timer_type)
  : timer_type(timer_type)
  {
    // must be initialized here so that all class members
    // are initialized
    trigger_thread = std::thread([this]() {
        timer_thread();
      });
  };

  ~TimerQueue()
  {
    stop();
  }

  void stop()
  {
    running = false;
    {
      std::scoped_lock l(mutex);
      wakeup_timer_thread();
    }
    if(trigger_thread.joinable())
    {
      trigger_thread.join();
    }
  }

  /**
   * @brief Removes a new timer from the queue.
   * This function is thread safe.
   *
   * Removes a timer, if it was added to this queue.
   * Ignores timers that are not part of this queue
   *
   * @param timer the timer to remove.
   */
  void remove_timer(const rclcpp::TimerBase::SharedPtr & timer)
  {
    rcl_clock_t * clock_type_of_timer;

    std::shared_ptr<const rcl_timer_t> handle = timer->get_timer_handle();

    if (rcl_timer_clock(
        const_cast<rcl_timer_t *>(handle.get()),
        &clock_type_of_timer) != RCL_RET_OK)
    {
      assert(false);
    }

    if (clock_type_of_timer->type != timer_type) {
      // timer is handled by another queue
      return;
    }

    timer->clear_on_reset_callback();

    std::scoped_lock l(mutex);

    auto it = std::find_if(
      all_timers.begin(), all_timers.end(),
      [rcl_ref = timer->get_timer_handle()](const std::unique_ptr<TimerData> & d)
      {
        return d->rcl_ref == rcl_ref;
      });

    if (it != all_timers.end()) {
      const TimerData * data_ptr = it->get();

      auto it2 = std::find_if(
        running_timers.begin(), running_timers.end(), [data_ptr](const auto & e) {
          return e.second == data_ptr;
        });

      if(it2 != running_timers.end())
      {
        running_timers.erase(it2);
      }
      all_timers.erase(it);
    }

    wakeup_timer_thread();
  }

  /**
   * @brief Adds a new timer to the queue.
   * This function is thread safe.
   *
   * This function will ignore any timer, that has not a matching type
   *
   * @param timer the timer to add.
   * @param timer_ready_callback callback that should be called when the timer is ready.
   */
  void add_timer(
    const rclcpp::TimerBase::SharedPtr & timer,
    const std::function<void(const std::function<void()> executed_cb)> & timer_ready_callback)
  {
    rcl_clock_t * clock_type_of_timer;

    std::shared_ptr<const rcl_timer_t> handle = timer->get_timer_handle();

    if (rcl_timer_clock(
        const_cast<rcl_timer_t *>(handle.get()),
        &clock_type_of_timer) != RCL_RET_OK)
    {
      assert(false);
    }

    if (clock_type_of_timer->type != timer_type) {
      // timer is handled by another queue
      return;
    }

    std::unique_ptr<TimerData> data = std::make_unique<TimerData>(TimerData{std::move(handle), GetClockHelper::get_clock(*timer), false, std::move(timer_ready_callback)});

    timer->set_on_reset_callback(
      [data_ptr = data.get(), this](size_t) {
        std::scoped_lock l(mutex);
        if (!remove_if_dropped(data_ptr))
        {
          add_timer_to_running_map(data_ptr);
        }
      });

    {
      std::scoped_lock l(mutex);
      // this will wake up the timer thread if needed
      add_timer_to_running_map(data.get());

      all_timers.emplace_back(std::move(data) );
    }
  }

private:

  /**
   * Wakes the timer thread. Must be called under lock
   * by mutex
   */
  void wakeup_timer_thread()
  {
    if(used_clock_for_timers)
    {
//       RCUTILS_LOG_ERROR_NAMED("cm_executors::wakeup_timer_thread", "Cancleing sleep on clock");
      used_clock_for_timers->cancel_sleep_or_wait();
    }
    else
    {
//       RCUTILS_LOG_ERROR_NAMED("cm_executors::wakeup_timer_thread", "thread_conditional.notify_all()");
      thread_conditional.notify_all();
    }
  }

  /**
   * Checks if the timer is still referenced if not deletes it from the queue
   *
   * @param timer_data The timer to check
   * @return true if removed / invalid
   */
  bool remove_if_dropped(const TimerData * timer_data)
  {
    if (timer_data->rcl_ref.unique()) {
      // timer was deleted
      auto it = std::find_if(
        all_timers.begin(), all_timers.end(), [timer_data](const std::unique_ptr<TimerData> & e) {
          return timer_data == e.get();
        }
      );

      if (it != all_timers.end()) {
        all_timers.erase(it);
      }
      return true;
    }
    return false;
  }

  /**
   * @brief adds the given timer_data to the map of running timers, if valid.
   *
   * Advances the rcl timer.
   * Computes the next call time of the timer.
   * readds the timer to the map of running timers
   */
  void add_timer_to_running_map(TimerData * timer_data)
  {
    // timer can already be in the running list, if
    // e.g. reset was called on a running timer
    if(timer_data->in_running_list)
    {
      for(auto it = running_timers.begin() ; it != running_timers.end(); it++)
      {
        if(it->second == timer_data)
        {
          running_timers.erase(it);
          break;
        }
      }
      timer_data->in_running_list = false;
    }

    int64_t next_call_time;

    rcl_ret_t ret = rcl_timer_get_next_call_time(timer_data->rcl_ref.get(), &next_call_time);

    if (ret != RCL_RET_OK) {
      return;
    }

    bool wasEmpty = running_timers.empty();
    std::chrono::nanoseconds old_next_call_time(-1);
    if(!wasEmpty)
    {
      old_next_call_time = running_timers.begin()->first;
    }

    running_timers.emplace(next_call_time, timer_data);
    timer_data->in_running_list = true;

    if(wasEmpty || old_next_call_time != running_timers.begin()->first)
    {
      // the next wakeup is now earlier, wake up the timer thread so that it can pick up the timer
      wakeup_timer_thread();
    }
  }

  void call_ready_timer_callbacks()
  {
    while (!running_timers.empty()) {

      if(remove_if_dropped(running_timers.begin()->second))
      {
        running_timers.erase(running_timers.begin());
        continue;
      }

      int64_t time_until_call;

      const rcl_timer_t * rcl_timer_ref = running_timers.begin()->second->rcl_ref.get();
      auto ret = rcl_timer_get_time_until_next_call(rcl_timer_ref, &time_until_call);
      if (ret == RCL_RET_TIMER_CANCELED) {
        running_timers.begin()->second->in_running_list = false;
        running_timers.erase(running_timers.begin());
        continue;
      }

      if (time_until_call <= 0) {
//         RCUTILS_LOG_ERROR_NAMED("cm_executors::timer_thread", "Timer ready, cur call time is %+" PRId64 , running_timers.begin()->first.count());

        // timer is ready, call ready callback to make the scheduler pick it up
        running_timers.begin()->second->timer_ready_callback(
          [timer_data = running_timers.begin()->second, this] ()
          {
            // Note, we have the guarantee, that the shared_ptr to this timer is
            // valid in case this callback is executed, as the executor holds a
            // reference to the timer during execution and at the time of this callback.
            // Therefore timer_data is valid.
            {
              std::scoped_lock l(mutex);
              add_timer_to_running_map(timer_data);
            }
//             RCUTILS_LOG_ERROR_NAMED("cm_executors::timer_thread", "Timer was executed, readding to map, waking timer_thread");
          }
        );

        // remove timer from, running list, until it was executed
        // the scheduler will readd the timer after execution
        running_timers.begin()->second->in_running_list = false;
        running_timers.erase(running_timers.begin());

        continue;
      }
      else
      {
//         RCUTILS_LOG_ERROR_NAMED("cm_executors::timer_thread", "Timer NOT ready, next call time is %+" PRId64 , running_timers.begin()->first.count());
      }
      break;
    }
  }

  void timer_thread()
  {
    while (running && rclcpp::ok()) {
      std::chrono::nanoseconds next_wakeup_time;
      {
        std::scoped_lock l(mutex);
        call_ready_timer_callbacks();

        if(running_timers.empty())
        {
          used_clock_for_timers.reset();
        }
        else
        {
          used_clock_for_timers = running_timers.begin()->second->clock;
          next_wakeup_time = running_timers.begin()->first;
        }
      }
      if(used_clock_for_timers)
      {
        try {
          used_clock_for_timers->wait_until_started();

//           RCUTILS_LOG_ERROR_NAMED("cm_executors::timer_thread", "has running timer, using clock to sleep");
          used_clock_for_timers->sleep_until(rclcpp::Time(next_wakeup_time.count(), timer_type));
//           RCUTILS_LOG_ERROR_NAMED("cm_executors::timer_thread", "sleep finished, or interrupted ");
        } catch (const std::runtime_error &) {
          //there is a race on shutdown, were the context may become invalid, while we call sleep_until
          running = false;
        }
      }
      else
      {
//         RCUTILS_LOG_ERROR_NAMED("cm_executors::timer_thread", "no running timer, waiting on thread_conditional");
        std::unique_lock l(mutex);
        thread_conditional.wait(l, [this]() {

//           RCUTILS_LOG_ERROR_NAMED("cm_executors::timer_thread", "thread_conditional: signal received : evaluation wakeup");
          return !running_timers.empty() || !running || !rclcpp::ok();

        });
//         RCUTILS_LOG_ERROR_NAMED("cm_executors::timer_thread", "woken up");
      }
    }
    thread_terminated = true;
  }

  rcl_clock_type_t timer_type;

  Context::SharedPtr clock_sleep_context;

  rclcpp::Clock::SharedPtr used_clock_for_timers;




  std::mutex mutex;

  std::atomic_bool running = true;
  std::atomic_bool thread_terminated = false;

  std::vector<std::unique_ptr<TimerData>> all_timers;

  using TimerMap = std::multimap<std::chrono::nanoseconds, TimerData *>;
  TimerMap running_timers;

  std::thread trigger_thread;

  std::condition_variable thread_conditional;
};

class TimerManager
{
  std::array<TimerQueue, 3> timer_queues;

public:
  TimerManager()
  : timer_queues{RCL_ROS_TIME, RCL_SYSTEM_TIME, RCL_STEADY_TIME}
  {

  }

  void remove_timer(const rclcpp::TimerBase::SharedPtr & timer)
  {
    for (TimerQueue & q : timer_queues) {
      q.remove_timer(timer);
    }
  }

  void add_timer(
    const rclcpp::TimerBase::SharedPtr & timer,
    const std::function<void(const std::function<void()> executed_cb)> & timer_ready_callback)
  {
    for (TimerQueue & q : timer_queues) {
      q.add_timer(timer, timer_ready_callback);
    }
  }

  void stop()
  {
    for (TimerQueue & q : timer_queues) {
      q.stop();
    }
  }
};
}
