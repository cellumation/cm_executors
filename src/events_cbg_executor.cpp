#include <cm_executors/events_cbg_executor.hpp>


// Copyright 2024 Cellumation GmbH
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

// #include "rclcpp/executors/cbg_executor.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include "rcpputils/scope_exit.hpp"
#include "rclcpp/exceptions/exceptions.hpp"
#include "rclcpp/detail/add_guard_condition_to_rcl_wait_set.hpp"

#include "rclcpp/logging.hpp"
#include "rclcpp/node.hpp"
#include <inttypes.h>
#include "callback_group_scheduler.hpp"
#include "timer_manager.hpp"
// #include "rclcpp/executors/detail/any_executable_weak_ref.hpp"

namespace rclcpp::executors
{

std::atomic<uint64_t> GlobalEventIdProvider::last_event_id = 1;

struct GuardConditionWithFunction
{
  GuardConditionWithFunction(rclcpp::GuardCondition::SharedPtr gc, std::function<void(void)> fun)
  : guard_condition(std::move(gc)), handle_guard_condition_fun(std::move(fun))
  {
  }

  rclcpp::GuardCondition::SharedPtr guard_condition;

  // A function that should be executed if the guard_condition is ready
  std::function<void(void)> handle_guard_condition_fun;
};

template<class ExecutableType_T>
struct WeakExecutableWithScheduler
{
  WeakExecutableWithScheduler(
    const std::shared_ptr<ExecutableType_T> & executable,
    CallbackGroupSchedulerEv * scheduler)
  : executable(executable), scheduler(scheduler)
  {
  }

  using ExecutableType = ExecutableType_T;

//  WeakExecutableWithRclHandle(WeakExecutableWithRclHandle &&) = default;
/*
   WeakExecutableWithRclHandle& operator= (const WeakExecutableWithRclHandle &) = delete;*/


  std::weak_ptr<ExecutableType> executable;
  CallbackGroupSchedulerEv * scheduler;

  bool processed;

  bool executable_alive()
  {
    auto use_cnt = executable.use_count();
    if (use_cnt == 0) {
      return false;
    }

    return true;
  }
};

using WeakTimerRef = WeakExecutableWithScheduler<rclcpp::TimerBase>;

using WeakSubscriberRef = WeakExecutableWithScheduler<rclcpp::SubscriptionBase>;
using WeakClientRef = WeakExecutableWithScheduler<rclcpp::ClientBase>;
using WeakServiceRef = WeakExecutableWithScheduler<rclcpp::ServiceBase>;
using WeakWaitableRef = WeakExecutableWithScheduler<rclcpp::Waitable>;


struct GloablaWeakExecutableCache
{
  std::vector<GuardConditionWithFunction> guard_conditions;

  ~GloablaWeakExecutableCache()
  {
    for (const auto & gc_ref : guard_conditions) {
      gc_ref.guard_condition->set_on_trigger_callback(nullptr);
    }
  }

  void add_guard_condition_event(
    rclcpp::GuardCondition::SharedPtr ptr,
    std::function<void(void)> fun)
  {
    guard_conditions.emplace_back(GuardConditionWithFunction(ptr, std::move(fun) ) );


    for (auto & entry : guard_conditions) {
      entry.guard_condition->set_on_trigger_callback(
        [ptr = &entry](size_t nr_events) {
          for (size_t i = 0; i < nr_events; i++) {
            if (ptr->handle_guard_condition_fun) {
              ptr->handle_guard_condition_fun();
            }
          }
        });
    }
  }

  void clear()
  {
    guard_conditions.clear();
  }

};

struct WeakExecutableCache
{
  WeakExecutableCache(CallbackGroupSchedulerEv & scheduler, TimerManager & timer_manager)
  : scheduler(scheduler),
    timer_manager(timer_manager)
  {
  }

  std::vector<WeakTimerRef> timers;
  std::vector<WeakSubscriberRef> subscribers;
  std::vector<WeakClientRef> clients;
  std::vector<WeakServiceRef> services;
  std::vector<WeakWaitableRef> waitables;
  std::vector<GuardConditionWithFunction> guard_conditions;

  CallbackGroupSchedulerEv & scheduler;

  TimerManager & timer_manager;

  ~WeakExecutableCache()
  {
    for (const auto & timer_ref : timers) {
      auto shr_ptr = timer_ref.executable.lock();
      if (shr_ptr) {
        shr_ptr->clear_on_reset_callback();
      }
    }
    for (const auto & timer_ref : subscribers) {
      auto shr_ptr = timer_ref.executable.lock();
      if (shr_ptr) {
        shr_ptr->clear_on_new_message_callback();
      }
    }
    for (const auto & timer_ref : clients) {
      auto shr_ptr = timer_ref.executable.lock();
      if (shr_ptr) {
        shr_ptr->clear_on_new_response_callback();
      }
    }
    for (const auto & timer_ref : services) {
      auto shr_ptr = timer_ref.executable.lock();
      if (shr_ptr) {
        shr_ptr->clear_on_new_request_callback();
      }
    }
    for (const auto & timer_ref : waitables) {
      auto shr_ptr = timer_ref.executable.lock();
      if (shr_ptr) {
        shr_ptr->clear_on_ready_callback();
      }
    }
    for (const auto & gc_ref : guard_conditions) {
      gc_ref.guard_condition->set_on_trigger_callback(nullptr);
    }
  }

//     bool cache_ditry = true;

  void clear()
  {
    timers.clear();
    subscribers.clear();
    clients.clear();
    services.clear();
    waitables.clear();
    guard_conditions.clear();
  }


  void regenerate_events(const rclcpp::CallbackGroup::SharedPtr & callback_group)
  {
    clear();

//           RCUTILS_LOG_ERROR_NAMED("rclcpp", "FOOOOO regenerate_events");


    // we reserve to much memory here, this this should be fine
    if (timers.capacity() == 0) {
      timers.reserve(callback_group->size() );
      subscribers.reserve(callback_group->size() );
      clients.reserve(callback_group->size() );
      services.reserve(callback_group->size() );
      waitables.reserve(callback_group->size() );
    }

    const auto add_sub = [this](const rclcpp::SubscriptionBase::SharedPtr & s) {
        //element not found, add new one
//       auto &entry = subscribers.emplace_back(WeakSubscriberRef(s, &scheduler));
        s->set_on_new_message_callback(
          [weak_ptr = rclcpp::SubscriptionBase::WeakPtr(s), this](size_t nr_msg) mutable {
//                 RCUTILS_LOG_ERROR_NAMED("rclcpp", "subscriber got data");
            for (size_t i = 0; i < nr_msg; i++) {
              scheduler.add_ready_executable(weak_ptr);
            }
          });
      };
    const auto add_timer = [this](const rclcpp::TimerBase::SharedPtr & s) {
        timer_manager.add_timer(
          s, [weak_ptr = rclcpp::TimerBase::WeakPtr(s), this]() mutable {
//                 RCUTILS_LOG_ERROR_NAMED("rclcpp", "added timer executable");

            scheduler.add_ready_executable(weak_ptr);
          });
      };

    const auto add_client = [this](const rclcpp::ClientBase::SharedPtr & s) {
        s->set_on_new_response_callback(
          [weak_ptr = rclcpp::ClientBase::WeakPtr(s), this](size_t nr_msg) mutable {
            for (size_t i = 0; i < nr_msg; i++) {
              scheduler.add_ready_executable(weak_ptr);
            }
          });
      };

    const auto add_service = [this](const rclcpp::ServiceBase::SharedPtr & s) {
        s->set_on_new_request_callback(
          [weak_ptr = rclcpp::ServiceBase::WeakPtr(s), this](size_t nr_msg) mutable {
            for (size_t i = 0; i < nr_msg; i++) {
              scheduler.add_ready_executable(weak_ptr);
            }
          });
      };

    auto cbFun = [weak_ptr = rclcpp::CallbackGroup::WeakPtr(callback_group), this]() {

//             RCUTILS_LOG_ERROR_NAMED("rclcpp", "GC: Callback group was changed");

        if (!rclcpp::contexts::get_global_default_context()->shutdown_reason().empty()) {
          return;
        }

        if (!rclcpp::ok(rclcpp::contexts::get_global_default_context())) {
          return;
        }

        rclcpp::CallbackGroup::SharedPtr cbg = weak_ptr.lock();

        if (cbg) {
          regenerate_events(cbg);
        }
      };

    add_guard_condition_event(
      callback_group->get_notify_guard_condition(), [cbFun, this]() {
//             RCUTILS_LOG_INFO("Callback group nofity callback !!!!!!!!!!!!");


        scheduler.add_ready_executable(CallbackEventType(cbFun));
      }
    );

    const auto add_waitable = [this](const rclcpp::Waitable::SharedPtr & s) {
        s->set_on_ready_callback(
          [weak_ptr = rclcpp::Waitable::WeakPtr(s), this](size_t nr_msg,
          int internal_ev_type) mutable {
//             RCUTILS_LOG_INFO("Waitable read, ev cnt : %lu", nr_msg);
            for (size_t i = 0; i < nr_msg; i++) {
              scheduler.add_ready_executable(WaitableWithEventType{weak_ptr, internal_ev_type});
            }
          });
      };


    callback_group->collect_all_ptrs(add_sub, add_service, add_client, add_timer, add_waitable);

//     RCUTILS_LOG_ERROR_NAMED("rclcpp", "GC: regeneraetd, new size : t %lu, sub %lu, c %lu, s %lu, gc %lu, waitables %lu", wait_set_size.timers, wait_set_size.subscriptions, wait_set_size.clients, wait_set_size.services, wait_set_size.guard_conditions, waitables.size());
  }

  void add_guard_condition_event(
    rclcpp::GuardCondition::SharedPtr ptr,
    std::function<void(void)> fun)
  {
    auto & new_entry = guard_conditions.emplace_back(
      GuardConditionWithFunction(
        ptr, std::move(
          fun) ) );

    if (new_entry.handle_guard_condition_fun) {
      new_entry.guard_condition->set_on_trigger_callback(
        [fun2 = new_entry.handle_guard_condition_fun, this](size_t /*nr_events*/) {
          scheduler.add_ready_executable(CallbackEventType(fun2));
        });
    }
  }
};


EventsCBGExecutor::EventsCBGExecutor(
  const rclcpp::ExecutorOptions & options,
  size_t number_of_threads,
  std::chrono::nanoseconds next_exec_timeout)
: next_exec_timeout_(next_exec_timeout),
  spinning(false),
  interrupt_guard_condition_(std::make_shared<rclcpp::GuardCondition>(options.context) ),
  shutdown_guard_condition_(std::make_shared<rclcpp::GuardCondition>(options.context) ),
  context_(options.context),
  timer_manager(std::make_unique<TimerManager>()),
  global_executable_cache(std::make_unique<GloablaWeakExecutableCache>() ),
  nodes_executable_cache(std::make_unique<GloablaWeakExecutableCache>() )
{

//     global_executable_cache->add_guard_condition_event (
//         interrupt_guard_condition_,
//         std::function<void ( void ) >() );
  global_executable_cache->add_guard_condition_event(
    shutdown_guard_condition_, [this]() {

//         RCUTILS_LOG_ERROR_NAMED ("rclcpp", "Shutdown guard condition triggered !");

      remove_all_nodes_and_callback_groups();

      spinning = false;

        wakeup_all_processing_threads();

      //FIXME deadlock
//         timer_manager->stop();
    });


  number_of_threads_ = number_of_threads > 0 ?
    number_of_threads :
    std::max(std::thread::hardware_concurrency(), 2U);

  shutdown_callback_handle_ = context_->add_on_shutdown_callback(
    [weak_gc = std::weak_ptr<rclcpp::GuardCondition> {shutdown_guard_condition_}]() {
      auto strong_gc = weak_gc.lock();
      if (strong_gc) {
        strong_gc->trigger();
      }
    });

  rcl_allocator_t allocator = options.memory_strategy->get_allocator();

  rcl_ret_t ret = rcl_wait_set_init(
    &wait_set_,
    0, 0, 0, 0, 0, 0,
    context_->get_rcl_context().get(),
    allocator);
  if (RCL_RET_OK != ret) {
    RCUTILS_LOG_ERROR_NAMED(
      "rclcpp",
      "failed to create wait set: %s", rcl_get_error_string().str);
    rcl_reset_error();
    exceptions::throw_from_rcl_error(ret, "Failed to create wait set in Executor constructor");
  }

}

EventsCBGExecutor::~EventsCBGExecutor()
{
  //we need to shut down the timer manager first, as it might access the Schedulers
  timer_manager.reset();

  // signal all processing threads to shut down
  spinning = false;

  wakeup_all_processing_threads();

  remove_all_nodes_and_callback_groups();

  // Remove shutdown callback handle registered to Context
  if (!context_->remove_on_shutdown_callback(shutdown_callback_handle_) ) {
    RCUTILS_LOG_ERROR_NAMED(
      "rclcpp",
      "failed to remove registered on_shutdown callback");
    rcl_reset_error();
  }

}

void EventsCBGExecutor::wakeup_processing_thread()
{
  {
    std::unique_lock lk(conditional_mutex);
    unprocessed_wakeups++;
  }
  work_ready_conditional.notify_one();
}

void EventsCBGExecutor::wakeup_all_processing_threads()
{
  {
    std::unique_lock lk(conditional_mutex);
    unprocessed_wakeups++;
  }
  work_ready_conditional.notify_all();
}


void EventsCBGExecutor::remove_all_nodes_and_callback_groups()
{
  std::vector<node_interfaces::NodeBaseInterface::WeakPtr> added_nodes_cpy;
  {
    std::lock_guard lock{added_nodes_mutex_};
    added_nodes_cpy = added_nodes;
  }

  for (const node_interfaces::NodeBaseInterface::WeakPtr & node_weak_ptr : added_nodes_cpy) {
    const node_interfaces::NodeBaseInterface::SharedPtr & node_ptr = node_weak_ptr.lock();
    if (node_ptr) {
      remove_node(node_ptr, false);
    }
  }

  std::vector<rclcpp::CallbackGroup::WeakPtr> added_cbgs_cpy;
  {
    std::lock_guard lock{added_callback_groups_mutex_};
    added_cbgs_cpy = added_callback_groups;
  }

  for (const auto & weak_ptr : added_cbgs_cpy) {
    auto shr_ptr = weak_ptr.lock();
    if (shr_ptr) {
      remove_callback_group(shr_ptr, false);
    }
  }
}

bool EventsCBGExecutor::execute_ready_executables_until(
  const std::chrono::time_point<std::chrono::steady_clock> & stop_time)
{
  bool found_work = false;

  for (size_t i = CallbackGroupSchedulerEv::Priorities::Calls;
    i <= CallbackGroupSchedulerEv::Priorities::Waitable; i++)
  {
    CallbackGroupSchedulerEv::Priorities cur_prio(static_cast<CallbackGroupSchedulerEv::Priorities>(
        i ) );

    for (CallbackGroupData & cbg_with_data : callback_groups) {
      if (cbg_with_data.scheduler->execute_unprocessed_executable_until(stop_time, cur_prio) ) {
        if (std::chrono::steady_clock::now() >= stop_time) {
          return true;
        }
      }
    }
  }
//   RCUTILS_LOG_ERROR_NAMED("rclcpp", (std::string("execute_ready_executables_until had word ") + std::to_string(found_work)).c_str() );

  return found_work;
}


bool EventsCBGExecutor::get_next_ready_executable(AnyExecutableCbgEv & any_executable)
{
  if (!spinning.load() ) {
    return false;
  }

  std::lock_guard g(callback_groups_mutex);

  sync_callback_groups();

//     RCUTILS_LOG_ERROR_NAMED("rclcpp", "get_next_ready_executable");

  struct ReadyCallbacksWithSharedPtr
  {
    CallbackGroupData * data;
    rclcpp::CallbackGroup::SharedPtr callback_group;
    bool ready = true;
  };

  std::vector<ReadyCallbacksWithSharedPtr> ready_callbacks;
  ready_callbacks.reserve(callback_groups.size() );


  for (auto it = callback_groups.begin(); it != callback_groups.end(); ) {
    CallbackGroupData & cbg_with_data(*it);

    ReadyCallbacksWithSharedPtr e;
    e.callback_group = cbg_with_data.callback_group.lock();
    if (!e.callback_group) {
      it = callback_groups.erase(it);
      continue;
    }

    if (e.callback_group->can_be_taken_from().load() ) {
      e.data = &cbg_with_data;
      ready_callbacks.push_back(std::move(e) );
    }

    it++;
  }

  bool found_work = false;

  for (size_t i = CallbackGroupSchedulerEv::Priorities::Calls;
    i <= CallbackGroupSchedulerEv::Priorities::Waitable; i++)
  {
    CallbackGroupSchedulerEv::Priorities cur_prio(static_cast<CallbackGroupSchedulerEv::Priorities>(
        i ) );
    for (ReadyCallbacksWithSharedPtr & ready_elem: ready_callbacks) {

      if (!found_work) {
        if (ready_elem.data->scheduler->get_unprocessed_executable(any_executable, cur_prio) ) {
          // mark callback group as in use
          ready_elem.callback_group->can_be_taken_from().store(false);
          any_executable.callback_group = ready_elem.callback_group;
          found_work = true;
          ready_elem.ready = false;
//                     RCUTILS_LOG_ERROR_NAMED("rclcpp", "get_next_ready_executable : found ready executable");
        }
      } else {
        if (ready_elem.ready && ready_elem.data->scheduler->has_unprocessed_executables() ) {
          //wake up worker thread
          wakeup_processing_thread();
          return true;
        }
      }
    }
  }

  return found_work;
}

size_t
EventsCBGExecutor::get_number_of_threads()
{
  return number_of_threads_;
}

void EventsCBGExecutor::sync_callback_groups()
{
  if (!needs_callback_group_resync.exchange(false) ) {
    return;
  }
//     RCUTILS_LOG_ERROR_NAMED("rclcpp", "sync_callback_groups");

  std::vector<std::pair<CallbackGroupData *, rclcpp::CallbackGroup::SharedPtr>> cur_group_data;
  cur_group_data.reserve(callback_groups.size() );

  for (CallbackGroupData & d : callback_groups) {
    auto p = d.callback_group.lock();
    if (p) {
      cur_group_data.emplace_back(&d, std::move(p) );
    }
  }

//     RCUTILS_LOG_ERROR_NAMED("rclcpp", "sync_callback_groups before lock");
//     std::scoped_lock<std::mutex> lk ( callback_groups_mutex );
//     RCUTILS_LOG_ERROR_NAMED("rclcpp", "sync_callback_groups after lock");
  std::vector<CallbackGroupData> next_group_data;

  std::set<CallbackGroup *> added_cbgs;

  auto insert_data =
    [&cur_group_data, &next_group_data, &added_cbgs,
      this](rclcpp::CallbackGroup::SharedPtr && cbg) {
      // nodes may share callback groups, therefore we need to make sure we only add them once
      if (added_cbgs.find(cbg.get() ) != added_cbgs.end() ) {
        return;
      }

      added_cbgs.insert(cbg.get() );

      for (const auto & pair : cur_group_data) {
        if (pair.second == cbg) {
//                 RCUTILS_LOG_INFO("Using existing callback group");
          next_group_data.push_back(std::move(*pair.first) );
          return;
        }
      }

//         RCUTILS_LOG_INFO("Using new callback group");

      CallbackGroupData new_entry;
      new_entry.scheduler = std::make_unique<CallbackGroupSchedulerEv>(work_ready_conditional);
      new_entry.executable_cache = std::make_unique<WeakExecutableCache>(
        *new_entry.scheduler,
        *timer_manager);
      new_entry.executable_cache->regenerate_events(cbg);
      new_entry.callback_group = std::move(cbg);
      next_group_data.push_back(std::move(new_entry) );
    };

  {
    std::vector<rclcpp::CallbackGroup::WeakPtr> added_cbgs_cpy;
    {
      std::lock_guard lock{added_callback_groups_mutex_};
      added_cbgs_cpy = added_callback_groups;
    }

    std::vector<node_interfaces::NodeBaseInterface::WeakPtr> added_nodes_cpy;
    {
      std::lock_guard lock{added_nodes_mutex_};
      added_nodes_cpy = added_nodes;
    }

    // *3 ist a rough estimate of how many callback_group a node may have
    next_group_data.reserve(added_cbgs_cpy.size() + added_nodes_cpy.size() * 3);

    nodes_executable_cache->clear();
//         nodes_executable_cache->guard_conditions.reserve(added_nodes_cpy.size());

//         RCUTILS_LOG_ERROR("Added node size is %lu", added_nodes_cpy.size());

    for (const node_interfaces::NodeBaseInterface::WeakPtr & node_weak_ptr : added_nodes_cpy) {
      auto node_ptr = node_weak_ptr.lock();
      if (node_ptr) {
        node_ptr->for_each_callback_group(
          [&insert_data](rclcpp::CallbackGroup::SharedPtr cbg) {
            if (cbg->automatically_add_to_executor_with_node() ) {
              insert_data(std::move(cbg) );
            }
          });

        // register node guard condition, and trigger resync on node change event
        nodes_executable_cache->add_guard_condition_event(
          node_ptr->get_shared_notify_guard_condition(),
          [this]() {
//             RCUTILS_LOG_INFO("Node changed GC triggered");
            needs_callback_group_resync.store(true);
            wakeup_processing_thread();
          });
      }
    }

    for (const rclcpp::CallbackGroup::WeakPtr & cbg : added_cbgs_cpy) {
      auto p = cbg.lock();
      if (p) {
        insert_data(std::move(p) );
      }
    }
  }

  callback_groups.swap(next_group_data);
}


void EventsCBGExecutor::do_housekeeping()
{
  using namespace rclcpp::exceptions;

  sync_callback_groups();

  {
//         std::lock_guard g ( callback_groups_mutex );
//         for ( CallbackGroupData & cbg_with_data: callback_groups ) {
//
//             // don't add anything to a waitset that has unprocessed data
//             if ( cbg_with_data.scheduler->has_unprocessed_executables() ) {
//                 continue;
//             }
//
//             auto cbg_shr_ptr = cbg_with_data.callback_group.lock();
//             if ( !cbg_shr_ptr ) {
//                 continue;
//             }
//
//             if ( !cbg_shr_ptr->can_be_taken_from() ) {
//                 continue;
//             }
//
//             //     CallbackGroupState & cbg_state = *cbg_with_data.callback_group_state;
//             // regenerate the state data
//             if ( cbg_with_data.executable_cache->cache_ditry ) {
//                 //       RCUTILS_LOG_INFO("Regenerating callback group");
//                 // Regenerating clears the dirty flag
//                 cbg_with_data.executable_cache->regenerate_events ( cbg_shr_ptr );
//             }
//         }
  }

}

void
EventsCBGExecutor::run(size_t this_thread_number)
{
  (void) this_thread_number;

  while (rclcpp::ok(this->context_) && spinning.load() ) {
    rclcpp::executors::AnyExecutableCbgEv any_exec;

    if (!get_next_ready_executable(any_exec) ) {

      {
        std::unique_lock lk(conditional_mutex);
        if(unprocessed_wakeups == 0)
        {
          if(!spinning)
          {
            return;
          }
//           RCUTILS_LOG_ERROR_NAMED("rclcpp", "going to sleep %lu", std::this_thread::get_id());
          work_ready_conditional.wait(lk);
//           RCUTILS_LOG_ERROR_NAMED("rclcpp", "woken up %lu", std::this_thread::get_id());
        }
        if(unprocessed_wakeups > 0)
        {
//             RCUTILS_LOG_ERROR_NAMED("rclcpp", "Decerease unprocessed work count %lu", unprocessed_wakeups);
            unprocessed_wakeups--;
        }
      }
      continue;
    }

//         RCUTILS_LOG_ERROR_NAMED("rclcpp", "Executing work %lu", std::this_thread::get_id());
    any_exec.execute_function();
    any_exec.callback_group->can_be_taken_from().store(true);

  }

//     RCUTILS_LOG_INFO("Stopping execution thread");
}

void EventsCBGExecutor::spin_once_internal(std::chrono::nanoseconds timeout)
{
  AnyExecutableCbgEv any_exec;
  {
    if (!rclcpp::ok(this->context_) || !spinning.load() ) {
      return;
    }

    if (!get_next_ready_executable(any_exec) ) {
//             RCUTILS_LOG_INFO("spin_once_internal: No work, going to sleep");

      if (timeout < std::chrono::nanoseconds::zero()) {
        // can't use std::chrono::nanoseconds::max, as wait_for
        // internally computes end time by using ::now() + timeout
        // as a workaround, we use some absurd high timeout
        timeout = std::chrono::hours(10000);
      }

      std::unique_lock lk(conditional_mutex);
      std::cv_status ret = work_ready_conditional.wait_for(lk, timeout);

      switch (ret) {
        case std::cv_status::no_timeout:
//                     RCUTILS_LOG_INFO("spin_once_internal: Woken up by trigger");
          break;
        case std::cv_status::timeout:
//                     RCUTILS_LOG_INFO("spin_once_internal: Woken up by timeout");
          break;
      }


      if (!get_next_ready_executable(any_exec) ) {
//                 RCUTILS_LOG_INFO("spin_once_internal: Still no work, return (timeout ?)");
        return;
      }
    }
  }

//     RCUTILS_LOG_INFO("spin_once_internal: Executing work");

  any_exec.execute_function();
  any_exec.callback_group->can_be_taken_from().store(true);
}

void
EventsCBGExecutor::spin_once(std::chrono::nanoseconds timeout)
{
  if (spinning.exchange(true) ) {
    throw std::runtime_error("spin_once() called while already spinning");
  }
  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );

  sync_callback_groups();

  spin_once_internal(timeout);
}


void
EventsCBGExecutor::spin_some(std::chrono::nanoseconds max_duration)
{
  collect_and_execute_ready_events(max_duration, false);
}

void EventsCBGExecutor::spin_all(std::chrono::nanoseconds max_duration)
{
  if (max_duration < std::chrono::nanoseconds::zero() ) {
    throw std::invalid_argument("max_duration must be greater than or equal to 0");
  }

  collect_and_execute_ready_events(max_duration, true);
}

bool EventsCBGExecutor::collect_and_execute_ready_events(
  std::chrono::nanoseconds max_duration,
  bool recollect_if_no_work_available)
{
  if (spinning.exchange(true) ) {
    throw std::runtime_error("collect_and_execute_ready_events() called while already spinning");
  }
  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );

  sync_callback_groups();

  const auto start = std::chrono::steady_clock::now();
  const auto end_time = start + max_duration;
  auto cur_time = start;

  // collect any work, that is already ready
//   wait_for_work(std::chrono::nanoseconds::zero(), true);

  bool got_work_since_collect = false;
  bool first_collect = true;
  bool had_work = false;

  //FIXME this is super hard to do, we need to know when to stop

  while (rclcpp::ok(this->context_) && spinning && cur_time <= end_time) {
    if (!execute_ready_executables_until(end_time) ) {

      if (!first_collect && !recollect_if_no_work_available) {
        // we are done
        return had_work;
      }

      if (first_collect || got_work_since_collect) {
        // wait for new work

//                 std::unique_lock lk(conditional_mutex);
//                 work_ready_conditional.wait_for(lk, std::chrono::nanoseconds::zero());
//                 wait_for_work(std::chrono::nanoseconds::zero(), !first_collect);

        first_collect = false;
        got_work_since_collect = false;
        continue;
      }

      return had_work;
    } else {
      got_work_since_collect = true;
    }

    cur_time = std::chrono::steady_clock::now();
  }

  return had_work;
}
void
EventsCBGExecutor::spin()
{
//     RCUTILS_LOG_ERROR_NAMED(
//         "rclcpp",
//         "EventsCBGExecutor::spin()");


  if (spinning.exchange(true) ) {
    throw std::runtime_error("spin() called while already spinning");
  }
  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );
  std::vector<std::thread> threads;
  size_t thread_id = 0;
  {
    std::lock_guard wait_lock{wait_mutex_};
    for ( ; thread_id < number_of_threads_ - 1; ++thread_id) {
      auto func = std::bind(&EventsCBGExecutor::run, this, thread_id);
      threads.emplace_back(func);
    }
  }

  run(thread_id);
  for (auto & thread : threads) {
    thread.join();
  }
}

void
EventsCBGExecutor::add_callback_group(
  rclcpp::CallbackGroup::SharedPtr group_ptr,
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr /*node_ptr*/,
  bool notify)
{
  {
    std::lock_guard lock{added_callback_groups_mutex_};
    added_callback_groups.push_back(group_ptr);
  }
  needs_callback_group_resync = true;
  wakeup_processing_thread();

  if (notify) {
    // Interrupt waiting to handle new node
    try {
      interrupt_guard_condition_->trigger();
    } catch (const rclcpp::exceptions::RCLError & ex) {
      throw std::runtime_error(
              std::string(
                "Failed to trigger guard condition on callback group add: ") + ex.what() );
    }
  }
}

void
EventsCBGExecutor::cancel()
{
  spinning.store(false);

  wakeup_all_processing_threads();

  try {
    interrupt_guard_condition_->trigger();
  } catch (const rclcpp::exceptions::RCLError & ex) {
    throw std::runtime_error(
            std::string("Failed to trigger guard condition in cancel: ") + ex.what() );
  }
}

std::vector<rclcpp::CallbackGroup::WeakPtr>
EventsCBGExecutor::get_all_callback_groups()
{
  std::lock_guard lock{added_callback_groups_mutex_};
  return added_callback_groups;
}

void EventsCBGExecutor::unregister_event_callbacks(const rclcpp::CallbackGroup::SharedPtr & cbg)
const
{
  const auto remove_sub = [](const rclcpp::SubscriptionBase::SharedPtr & s) {
      s->clear_on_new_message_callback();
    };
  const auto remove_timer = [this](const rclcpp::TimerBase::SharedPtr & s) {
      timer_manager->remove_timer(s);
    };

  const auto remove_client = [](const rclcpp::ClientBase::SharedPtr & s) {
      s->clear_on_new_response_callback();
    };

  const auto remove_service = [](const rclcpp::ServiceBase::SharedPtr & s) {
      s->clear_on_new_request_callback();
    };

  auto gc_ptr = cbg->get_notify_guard_condition();
  if (gc_ptr) {
    gc_ptr->set_on_trigger_callback(std::function<void(size_t)>());
  }

  const auto remove_waitable = [](const rclcpp::Waitable::SharedPtr & s) {
      s->clear_on_ready_callback();
    };


  cbg->collect_all_ptrs(remove_sub, remove_service, remove_client, remove_timer, remove_waitable);
}


void
EventsCBGExecutor::remove_callback_group(
  rclcpp::CallbackGroup::SharedPtr group_ptr,
  bool notify)
{
  bool found = false;
  {
    std::lock_guard lock{added_callback_groups_mutex_};
    added_callback_groups.erase(
      std::remove_if(
        added_callback_groups.begin(), added_callback_groups.end(),
        [&group_ptr, &found](const auto & weak_ptr) {
          auto shr_ptr = weak_ptr.lock();
          if (!shr_ptr) {
            return true;
          }

          if (group_ptr == shr_ptr) {
            found = true;
            return true;
          }
          return false;
        }), added_callback_groups.end() );
    added_callback_groups.push_back(group_ptr);
  }

  //we need to unregister all callbacks
  unregister_event_callbacks(group_ptr);

  if (found) {
    needs_callback_group_resync = true;
    wakeup_processing_thread();
  }

  if (notify) {
    // Interrupt waiting to handle new node
    try {
      interrupt_guard_condition_->trigger();
    } catch (const rclcpp::exceptions::RCLError & ex) {
      throw std::runtime_error(
              std::string(
                "Failed to trigger guard condition on callback group add: ") + ex.what() );
    }
  }
}

void
EventsCBGExecutor::add_node(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
  bool notify)
{
  // If the node already has an executor
  std::atomic_bool & has_executor = node_ptr->get_associated_with_executor_atomic();
  if (has_executor.exchange(true) ) {
    throw std::runtime_error(
            std::string("Node '") + node_ptr->get_fully_qualified_name() +
            "' has already been added to an executor.");
  }

  {
    std::lock_guard lock{added_nodes_mutex_};
    added_nodes.push_back(node_ptr);
  }

  needs_callback_group_resync = true;

  wakeup_processing_thread();


//     node_ptr->get_notify_guard_condition();


  {
    std::lock_guard g(callback_groups_mutex);

    if (!spinning) {
      sync_callback_groups();
    }
  }
}

void
EventsCBGExecutor::add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  add_node(node_ptr->get_node_base_interface(), notify);
}

void
EventsCBGExecutor::remove_node(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
  bool notify)
{
  {
    std::lock_guard lock{added_nodes_mutex_};
    added_nodes.erase(
      std::remove_if(
        added_nodes.begin(), added_nodes.end(), [&node_ptr](const auto & weak_ptr) {
          const auto shr_ptr = weak_ptr.lock();
          if (shr_ptr && shr_ptr == node_ptr) {
            return true;
          }
          return false;
        }), added_nodes.end() );
  }

  node_ptr->for_each_callback_group(
    [this](rclcpp::CallbackGroup::SharedPtr cbg)
    {
      unregister_event_callbacks(cbg);
    }
  );

  needs_callback_group_resync = true;

  if (notify) {
    wakeup_processing_thread();
    // Interrupt waiting to handle new node
    try {
      interrupt_guard_condition_->trigger();
    } catch (const rclcpp::exceptions::RCLError & ex) {
      throw std::runtime_error(
              std::string(
                "Failed to trigger guard condition on callback group add: ") + ex.what() );
    }
  }

  node_ptr->get_associated_with_executor_atomic().store(false);
}

void
EventsCBGExecutor::remove_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  remove_node(node_ptr->get_node_base_interface(), notify);
}

// add a callback group to the executor, not bound to any node
void EventsCBGExecutor::add_callback_group_only(rclcpp::CallbackGroup::SharedPtr group_ptr)
{
  add_callback_group(group_ptr, nullptr, true);
}
}
