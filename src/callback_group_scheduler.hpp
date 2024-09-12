#pragma once
#include <functional>
#include <deque>
#include <chrono>
#include <rclcpp/timer.hpp>
#include <rclcpp/subscription_base.hpp>
#include <rclcpp/waitable.hpp>
#include <rclcpp/guard_condition.hpp>
#include <rclcpp/service.hpp>
#include <rclcpp/client.hpp>
#include <rclcpp/executor.hpp>

namespace rclcpp
{
namespace executors
{

class GlobalEventIdProvider
{
  static std::atomic<uint64_t> last_event_id;

public:
  // Returns the last id, returnd by getNextId
  static uint64_t get_last_id()
  {
    return last_event_id;
  }

  // increases the Id by one and returns the Id
  static uint64_t get_next_id()
  {
    return (++last_event_id);
  }
};

struct AnyExecutableCbgEv
{
  std::function<void()> execute_function;
  rclcpp::CallbackGroup::SharedPtr callback_group;
  bool more_executables_ready_in_cbg = false;
};

struct WaitableWithEventType
{
  rclcpp::Waitable::WeakPtr waitable;
  int internal_event_type;

  bool expired()
  {
    return waitable.expired();
  }
};

struct CallbackEventType
{
  CallbackEventType(std::function<void()> callback)
  : callback(callback)
  {
  }

  std::function<void()> callback;

  bool expired()
  {
    return false;
  }
};

template<class ExecutableRef>
class ExecutionQueue
{
public:
  ExecutionQueue()
  {
  }

  void add_ready_executable(const ExecutableRef & e, uint64_t event_id)
  {
    std::unique_lock lk(executable_mutex);
    ready_executables.emplace_back(e, event_id);
  }

  bool has_unprocessed_executables()
  {
    std::unique_lock lk(executable_mutex);
    while (!ready_executables.empty()) {
      if (!ready_executables.front().executable.expired()) {
        return true;
      }
      ready_executables.pop_front();

    }

    return false;
  }

  bool get_unprocessed_executable(AnyExecutableCbgEv & any_executable)
  {
    std::unique_lock lk(executable_mutex);
    while (!ready_executables.empty()) {
      if (fill_any_executable(any_executable, ready_executables.front().executable)) {
        ready_executables.pop_front();
        return true;
      }
      ready_executables.pop_front();
    }

    return false;
  }

  bool execute_unprocessed_executable_until(const uint64_t max_id,
    const std::chrono::time_point<std::chrono::steady_clock> & stop_time)
  {
    bool executed_executable = false;

    while (!ready_executables.empty()) {

      // RCUTILS_LOG_ERROR_NAMED("rclcpp", (std::string("execute_unprocessed_executable_until front id ") + std::to_string(ready_executables.front().event_id) + " max_id id is " + std::to_string(max_id)).c_str());
      if(ready_executables.front().event_id > max_id)
      {
        return executed_executable;
      }

      AnyExecutableCbgEv any_executable;
      if (fill_any_executable(any_executable, ready_executables.front().executable)) {
        ready_executables.pop_front();

        any_executable.execute_function();

        executed_executable = true;

        if (std::chrono::steady_clock::now() >= stop_time) {
          return true;
        }
      }
    }

    return executed_executable;
  }

  bool execute_unprocessed_executable_until(
    const std::chrono::time_point<std::chrono::steady_clock> & stop_time)
  {
    bool executed_executable = false;

    while (!ready_executables.empty()) {
      AnyExecutableCbgEv any_executable;
      if (fill_any_executable(any_executable, ready_executables.front().executable)) {
        ready_executables.pop_front();

        any_executable.execute_function();

        executed_executable = true;

        if (std::chrono::steady_clock::now() >= stop_time) {
          return true;
        }
      }
    }

    return executed_executable;
  }

private:
  bool fill_any_executable(
    AnyExecutableCbgEv & any_executable,
    const rclcpp::SubscriptionBase::WeakPtr & ptr)
  {
    auto shr_ptr = ptr.lock();
    if (!shr_ptr) {
      return false;
    }
    any_executable.execute_function = [shr_ptr = std::move(shr_ptr)]() {
        rclcpp::executors::EventsCBGExecutor::execute_subscription(shr_ptr);
      };

    return true;
  }
  bool fill_any_executable(
    AnyExecutableCbgEv & any_executable,
    const rclcpp::TimerBase::WeakPtr & ptr)
  {
    auto shr_ptr = ptr.lock();
    if (!shr_ptr) {
      return false;
    }
    auto data = shr_ptr->call();
    if (!data) {
      // timer was cancelled, skip it.
      return false;
    }

    any_executable.execute_function = [shr_ptr = std::move(shr_ptr), data = std::move(data)]() {
//         shr_ptr->execute_callback();
        rclcpp::executors::EventsCBGExecutor::execute_timer(shr_ptr, data);
      };

    return true;
  }
  bool fill_any_executable(
    AnyExecutableCbgEv & any_executable,
    const rclcpp::ServiceBase::WeakPtr & ptr)
  {
    auto shr_ptr = ptr.lock();
    if (!shr_ptr) {
      return false;
    }
    any_executable.execute_function = [shr_ptr = std::move(shr_ptr)]() {
        rclcpp::executors::EventsCBGExecutor::execute_service(shr_ptr);
      };

    return true;
  }
  bool fill_any_executable(
    AnyExecutableCbgEv & any_executable,
    const rclcpp::ClientBase::WeakPtr & ptr)
  {
    auto shr_ptr = ptr.lock();
    if (!shr_ptr) {
      return false;
    }
    any_executable.execute_function = [shr_ptr = std::move(shr_ptr)]() {
        rclcpp::executors::EventsCBGExecutor::execute_client(shr_ptr);
      };

    return true;
  }
  bool fill_any_executable(
    AnyExecutableCbgEv & any_executable,
    const WaitableWithEventType & ev)
  {
    auto shr_ptr_in = ev.waitable.lock();
    if (!shr_ptr_in) {
      return false;
    }
    auto data_in = shr_ptr_in->take_data_by_entity_id(ev.internal_event_type);

    any_executable.execute_function =
      [shr_ptr = std::move(shr_ptr_in), data = std::move(data_in)]() mutable {
//             RCUTILS_LOG_INFO("Before execute of waitable");
        shr_ptr->execute(static_cast<const std::shared_ptr<void>>(data));
      };

    return true;
  }

  bool fill_any_executable(
    AnyExecutableCbgEv & any_executable,
    const CallbackEventType & cbev)
  {
    any_executable.execute_function = cbev.callback;

    return true;
  }
  struct ExecutableRefWithAddTime
  {
    ExecutableRefWithAddTime(const ExecutableRef & ref, uint64_t event_id)
    : event_id(event_id),
      executable(ref)
    {
    }
    // a global monotonic id. This may be used to determine
    // if an event was used before or after a certain point in time
    uint64_t event_id;
    ExecutableRef executable;
  };

  std::mutex executable_mutex;
  std::deque<ExecutableRefWithAddTime> ready_executables;

};


class CallbackGroupSchedulerEv
{
public:
  enum SchedulingPolicy
  {
    // Execute all ready events in priority order
    FistInFirstOut,
    // Only execute the highest ready event
    Prioritized,
  };

  CallbackGroupSchedulerEv(
    std::condition_variable & work_available,
    SchedulingPolicy sched_policy = SchedulingPolicy::Prioritized)
  : sched_policy(sched_policy),
    work_available(work_available)
  {
  }

  void clear_and_prepare(
    const size_t max_timer, const size_t max_subs, const size_t max_services,
    const size_t max_clients, const size_t max_waitables);

  void add_ready_executable(rclcpp::SubscriptionBase::WeakPtr & executable)
  {
    ready_subscriptions.add_ready_executable(executable, GlobalEventIdProvider::get_next_id());
//         RCUTILS_LOG_INFO("Subscription got data");
    work_available.notify_one();
  }
  void add_ready_executable(rclcpp::ServiceBase::WeakPtr & executable)
  {
    ready_services.add_ready_executable(executable, GlobalEventIdProvider::get_next_id());
//         RCUTILS_LOG_INFO("Service got data");
    work_available.notify_one();
  }
  void add_ready_executable(rclcpp::TimerBase::WeakPtr & executable)
  {
    ready_timers.add_ready_executable(executable, GlobalEventIdProvider::get_next_id());
//         RCUTILS_LOG_INFO("Timer got data");
    work_available.notify_one();
  }
  void add_ready_executable(rclcpp::ClientBase::WeakPtr & executable)
  {
    ready_clients.add_ready_executable(executable, GlobalEventIdProvider::get_next_id());
//         RCUTILS_LOG_INFO("Client got data");
    work_available.notify_one();
  }
  void add_ready_executable(const WaitableWithEventType & executable)
  {
    ready_waitables.add_ready_executable(executable, GlobalEventIdProvider::get_next_id());
    // RCUTILS_LOG_INFO(("Waitable got data using id " + std::to_string(id)).c_str());
    work_available.notify_one();
  }
  void add_ready_executable(const CallbackEventType & executable)
  {
    ready_calls.add_ready_executable(executable, GlobalEventIdProvider::get_next_id());
//         RCUTILS_LOG_INFO("Callback got data");
    work_available.notify_one();
  }

  enum Priorities
  {
    Calls = 0,
    Timer,
    Subscription,
    Service,
    Client,
    Waitable
  };

  bool get_unprocessed_executable(AnyExecutableCbgEv & any_executable, enum Priorities for_priority)
  {
    switch (for_priority) {
      case Calls:
        return ready_calls.get_unprocessed_executable(any_executable);
        break;
      case Client:
        return ready_clients.get_unprocessed_executable(any_executable);
        break;
      case Service:
        return ready_services.get_unprocessed_executable(any_executable);
        break;
      case Subscription:
        return ready_subscriptions.get_unprocessed_executable(any_executable);
        break;
      case Timer:
        return ready_timers.get_unprocessed_executable(any_executable);
        break;
      case Waitable:
        return ready_waitables.get_unprocessed_executable(any_executable);
        break;
    }
    return false;
  }

  /**
   * @return true if something was executed
   */
  bool execute_unprocessed_executable_until(
    const std::chrono::time_point<std::chrono::steady_clock> & stop_time,
    enum Priorities for_priority)
  {
    switch (for_priority) {
      case Calls:
        return ready_calls.execute_unprocessed_executable_until(stop_time);
        break;
      case Client:
        return ready_clients.execute_unprocessed_executable_until(stop_time);
        break;
      case Service:
        return ready_services.execute_unprocessed_executable_until(stop_time);
        break;
      case Subscription:
        return ready_subscriptions.execute_unprocessed_executable_until(stop_time);
        break;
      case Timer:
        return ready_timers.execute_unprocessed_executable_until(stop_time);
        break;
      case Waitable:
        return ready_waitables.execute_unprocessed_executable_until(stop_time);
        break;
    }
    return false;
  }

    bool execute_unprocessed_executable_until(const uint64_t max_id,
    const std::chrono::time_point<std::chrono::steady_clock> & stop_time,
    enum Priorities for_priority)
  {
    switch (for_priority) {
      case Calls:
        return ready_calls.execute_unprocessed_executable_until(max_id, stop_time);
        break;
      case Client:
        return ready_clients.execute_unprocessed_executable_until(max_id, stop_time);
        break;
      case Service:
        return ready_services.execute_unprocessed_executable_until(max_id, stop_time);
        break;
      case Subscription:
        return ready_subscriptions.execute_unprocessed_executable_until(max_id, stop_time);
        break;
      case Timer:
        return ready_timers.execute_unprocessed_executable_until(max_id, stop_time);
        break;
      case Waitable:
        return ready_waitables.execute_unprocessed_executable_until(max_id, stop_time);
        break;
    }
    return false;
  }

  bool has_unprocessed_executables()
  {
    return ready_calls.has_unprocessed_executables() ||
           ready_subscriptions.has_unprocessed_executables() ||
           ready_timers.has_unprocessed_executables() ||
           ready_clients.has_unprocessed_executables() ||
           ready_services.has_unprocessed_executables() ||
           ready_waitables.has_unprocessed_executables();
  }

private:


  ExecutionQueue<rclcpp::TimerBase::WeakPtr> ready_timers;
  ExecutionQueue<rclcpp::SubscriptionBase::WeakPtr> ready_subscriptions;
  ExecutionQueue<rclcpp::ServiceBase::WeakPtr> ready_services;
  ExecutionQueue<rclcpp::ClientBase::WeakPtr> ready_clients;
  ExecutionQueue<WaitableWithEventType> ready_waitables;
  ExecutionQueue<CallbackEventType> ready_calls;

  SchedulingPolicy sched_policy;

  std::condition_variable & work_available;
};

}
}
