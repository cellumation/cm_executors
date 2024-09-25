#pragma once
#include "scheduler.hpp"
#include "timer_manager.hpp"
#include <rclcpp/executors/executor_entities_collection.hpp>

namespace rclcpp
{
namespace executors
{


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

using WeakTimerRef = WeakExecutableWithScheduler<rclcpp::TimerBase>;

using WeakSubscriberRef = WeakExecutableWithScheduler<rclcpp::SubscriptionBase>;
using WeakClientRef = WeakExecutableWithScheduler<rclcpp::ClientBase>;
using WeakServiceRef = WeakExecutableWithScheduler<rclcpp::ServiceBase>;
using WeakWaitableRef = WeakExecutableWithScheduler<rclcpp::Waitable>;


struct RegisteredEntityCache
{
    RegisteredEntityCache(CBGScheduler & scheduler, TimerManager & timer_manager, const rclcpp::CallbackGroup::SharedPtr & callback_group)
  :
  callback_group_weak_ptr(callback_group),
  scheduler_cbg_handle(*scheduler.add_callback_group(callback_group)),
    timer_manager(timer_manager)
  {
  }

  std::vector<WeakTimerRef> timers;
  std::vector<WeakSubscriberRef> subscribers;
  std::vector<WeakClientRef> clients;
  std::vector<WeakServiceRef> services;
  std::vector<WeakWaitableRef> waitables;
  std::vector<GuardConditionWithFunction> guard_conditions;

  rclcpp::CallbackGroup::WeakPtr callback_group_weak_ptr;
  CBGScheduler::CallbackGroupHandle & scheduler_cbg_handle;

  TimerManager & timer_manager;

  ~RegisteredEntityCache()
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

  void clear()
  {
    timers.clear();
    subscribers.clear();
    clients.clear();
    services.clear();
    waitables.clear();
    guard_conditions.clear();
  }


  bool regenerate_events()
  {
    clear();

//     RCUTILS_LOG_ERROR_NAMED("RegisteredEntityCache", "FOOOOO regenerate_events");

    rclcpp::CallbackGroup::SharedPtr callback_group = callback_group_weak_ptr.lock();

    if(!callback_group)
    {
        return false;
    }

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
            scheduler_cbg_handle.get_ready_callback_for_entity(s));
      };
    const auto add_timer = [this](const rclcpp::TimerBase::SharedPtr & s) {

//         RCUTILS_LOG_ERROR_NAMED("RegisteredEntityCache", "add_timer called for callback group");

        // we 'miss use'  this function, to make sure we don't add a timer two times
        if(!s->exchange_in_use_by_wait_set_state(true))
        {
          timer_manager.add_timer(s, scheduler_cbg_handle.get_ready_callback_for_entity(s));
        }
      };

    const auto add_client = [this](const rclcpp::ClientBase::SharedPtr & s) {
        s->set_on_new_response_callback(scheduler_cbg_handle.get_ready_callback_for_entity(s));
      };

    const auto add_service = [this](const rclcpp::ServiceBase::SharedPtr & s) {
        s->set_on_new_request_callback(scheduler_cbg_handle.get_ready_callback_for_entity(s));
      };



    const auto add_waitable = [this](const rclcpp::Waitable::SharedPtr & s) {
        s->set_on_ready_callback(scheduler_cbg_handle.get_ready_callback_for_entity(s));
      };

    auto cbg_guard_condition_handler = [this]() {

//             RCUTILS_LOG_ERROR_NAMED("rclcpp", "GC: Callback group was changed");

        if (!rclcpp::contexts::get_global_default_context()->shutdown_reason().empty()) {
          return;
        }

        if (!rclcpp::ok(rclcpp::contexts::get_global_default_context())) {
          return;
        }

        if(!regenerate_events())
        {
            //FIXME remove ???
        }
      };

    // We readd this function on every regen, as we just clear everything on every iterration
    add_guard_condition_event(
      callback_group->get_notify_guard_condition(), [cbg_guard_condition_handler, this]() {
//             RCUTILS_LOG_INFO("Callback group nofity callback !!!!!!!!!!!!");

          cbg_guard_condition_handler();
      }
    );

    callback_group->collect_all_ptrs(add_sub, add_service, add_client, add_timer, add_waitable);

//     RCUTILS_LOG_ERROR_NAMED("rclcpp", "GC: regeneraetd, new size : t %lu, sub %lu, c %lu, s %lu, gc %lu, waitables %lu", wait_set_size.timers, wait_set_size.subscriptions, wait_set_size.clients, wait_set_size.services, wait_set_size.guard_conditions, waitables.size());

    return true;
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
        scheduler_cbg_handle.get_ready_callback_for_entity(CBGScheduler::CallbackEventType(fun)));
    }
  }
};

}
}
