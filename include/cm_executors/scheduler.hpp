// Copyright 2024 Cellumation GmbH.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <utility>

#include <rclcpp/callback_group.hpp>
#include "global_event_id_provider.hpp"

namespace rclcpp
{
namespace executors
{

class CallbackGroupSchedulerEv;
struct WeakExecutableCache;
struct AnyExecutableCbgEv;
class TimerManager;
struct GloablaWeakExecutableCache;


class CBGScheduler
{
public:
  struct WaitableWithEventType
  {
    rclcpp::Waitable::WeakPtr waitable;
    int internal_event_type;

    bool expired() const
    {
      return waitable.expired();
    }
  };

  struct CallbackEventType
  {
    explicit CallbackEventType(std::function<void()> callback)
    : callback(callback)
    {
    }

    std::function<void()> callback;

    bool expired() const
    {
      return false;
    }
  };

  struct CallbackGroupHandle
  {
    explicit CallbackGroupHandle(CBGScheduler & scheduler)
    : scheduler(scheduler) {}

    virtual ~CallbackGroupHandle() = default;

    virtual std::function<void(size_t)> get_ready_callback_for_entity(
      const rclcpp::SubscriptionBase::WeakPtr & entity) = 0;
    virtual std::function<void(std::function<void()> executed_callback)>
    get_ready_callback_for_entity(const rclcpp::TimerBase::WeakPtr & entity) = 0;
    virtual std::function<void(size_t)> get_ready_callback_for_entity(
      const rclcpp::ClientBase::WeakPtr & entity) = 0;
    virtual std::function<void(size_t)> get_ready_callback_for_entity(
      const rclcpp::ServiceBase::WeakPtr & entity) = 0;
    virtual std::function<void(size_t,
      int)> get_ready_callback_for_entity(const rclcpp::Waitable::WeakPtr & entity) = 0;
    virtual std::function<void(size_t)> get_ready_callback_for_entity(
      const CallbackEventType & entity) = 0;

    void mark_as_executed()
    {
      std::lock_guard l(ready_mutex);
      not_ready = false;

      if(!has_ready_entities()) {
        idle = true;
      } else {
            // inform scheduler that we have more work
        scheduler.callback_group_ready(this);
      }
    }


    bool is_ready();

protected:
    CBGScheduler & scheduler;

    /**
     * Will always be called under lock of ready_mutex
     */
    virtual bool has_ready_entities() const = 0;

    /**
    * This function checks if the callback group
    * is currently idle, and may be moved into the
    * list of ready callback groups
    *
    * Must be called with ready_mutex locked
    */
    void check_move_to_ready()
    {
      if(not_ready) {
        return;
      }

      if(idle) {
        scheduler.callback_group_ready(this);
        idle = false;
      }
    }

    void mark_as_skiped()
    {
      if(!has_ready_entities()) {
        idle = true;
      }
//       else
//       {
//           throw std::runtime_error("Internal error, group marked as skipped,"
//                                    " but work was ready");
//       }
    }
    std::mutex ready_mutex;

private:
    // will be set if cbg is mutual exclusive and something is executing
    bool not_ready = false;

    // true, if nothing is beeing executed, and there are no pending events
    bool idle = true;
  };

  struct ExecutableEntity
  {
    // if called executes the entitiy
    std::function<void()> execute_function;
    // The callback group associated with the entity. Can be nullptr.
    CallbackGroupHandle *callback_handle = nullptr;
  };

  CallbackGroupHandle * add_callback_group(const rclcpp::CallbackGroup::SharedPtr & callback_group)
  {
    auto uPtr = get_handle_for_callback_group(callback_group);
    CallbackGroupHandle * ret = uPtr.get();

    std::lock_guard lk(ready_callback_groups_mutex);

    callback_groups.push_back(std::move(uPtr));
    return ret;
  }

  void remove_callback_group(const CallbackGroupHandle *callback_handle)
  {
    std::lock_guard lk(ready_callback_groups_mutex);
    ready_callback_groups.erase(std::find(ready_callback_groups.begin(),
          ready_callback_groups.end(), callback_handle));

    callback_groups.remove_if([&callback_handle] (const auto & e) {
        return e.get() == callback_handle;
    });
  }

  // Will be called, by CallbackGroupHandle if any entity in the cb group is ready for execution
  // and the cb group was idle before
  void callback_group_ready(CallbackGroupHandle *handle)
  {
    {
//          RCUTILS_LOG_INFO_NAMED("CallbackGroupHandle", "CallbackGroupHandle moved to ready");
      std::lock_guard l(ready_callback_groups_mutex);
      ready_callback_groups.push_back(handle);
//          RCUTILS_LOG_INFO_NAMED("CallbackGroupHandle", ("Num ready CallbackGroupHandles : " +
//       std::to_string(ready_callback_groups.size())).c_str());
    }

    wakeup_one_worker_thread();
  }


  /**
   * Returns the next ready entity that shall be executed.
   * If a entity is removed here, the scheduler may assume that
   * it will be executed, and that the function mark_entity_as_executed
   * will be called afterwards.
   */
  virtual std::optional<ExecutableEntity> get_next_ready_entity() = 0;
  virtual std::optional<ExecutableEntity> get_next_ready_entity(
    GlobalEventIdProvider::MonotonicId max_id) = 0;

  /**
   * Must be called, after a entity was executed. This function will
   * normally be used, to mark the associated callback group as ready
   * again.
   */
  void mark_entity_as_executed(const ExecutableEntity & e)
  {
    if(e.callback_handle) {
      e.callback_handle->mark_as_executed();
    }
  }

  /**
   * This function inserts a dummy event into the scheduler, so
   * that a worker thread is unblocked, but does not execute any
   * work afterwards. This is essential a hack, to allow the
   * executor to perform its callback group syncing, without
   * returning, making the spin some function return
   */
  void unblock_one_worker_thread()
  {
    {
      std::lock_guard lk(ready_callback_groups_mutex);
      release_worker_once = true;
    }
    work_ready_conditional.notify_one();
  }

  void block_worker_thread()
  {
    std::unique_lock lk(ready_callback_groups_mutex);
    work_ready_conditional.wait(lk, [this]() -> bool {
        return !ready_callback_groups.empty() || release_worker_once || release_workers;
    });
    release_worker_once = false;
  }

  void block_worker_thread_for(std::chrono::nanoseconds timeout)
  {
    std::unique_lock lk(ready_callback_groups_mutex);
    work_ready_conditional.wait_for(lk, timeout, [this]() -> bool {
        return !ready_callback_groups.empty() || release_worker_once || release_workers;
    });
    release_worker_once = false;
  }

  void wakeup_one_worker_thread()
  {
    work_ready_conditional.notify_one();
  }

  void release_all_worker_threads()
  {
    {
      std::lock_guard lk(ready_callback_groups_mutex);
      release_workers = true;
    }
    work_ready_conditional.notify_all();
  }

protected:
  virtual std::unique_ptr<CallbackGroupHandle> get_handle_for_callback_group(
    const rclcpp::CallbackGroup::SharedPtr & callback_group) = 0;


  std::mutex ready_callback_groups_mutex;
  std::deque<CallbackGroupHandle *> ready_callback_groups;

  bool release_workers = false;
  bool release_worker_once = false;

  std::condition_variable work_ready_conditional;

  std::list<std::unique_ptr<CallbackGroupHandle>> callback_groups;
};
}  // namespace executors
}  // namespace rclcpp
