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
#include "PriorityScheduler.hpp"

namespace rclcpp
{
namespace executors
{

std::function<void(size_t)> PriorityCallbackGroupHandle::get_ready_callback_for_entity(
  const rclcpp::SubscriptionBase::WeakPtr & entity)
{
  return [weak_ptr = entity, this](size_t nr_msg) {
           std::lock_guard l(ready_mutex);

//                 RCUTILS_LOG_ERROR_NAMED("rclcpp", "subscriber got data");
           for (size_t i = 0; i < nr_msg; i++) {
             ready_subscriptions.emplace_back(weak_ptr);
           }

           check_move_to_ready();
         };
}

std::function<void(std::function<void()> executed_callback)> PriorityCallbackGroupHandle::
get_ready_callback_for_entity(const rclcpp::TimerBase::WeakPtr & entity)
{
  return [weak_ptr = entity, this](std::function<void()> executed_callback) {
           std::lock_guard l(ready_mutex);
//         RCUTILS_LOG_INFO_NAMED("FirstInFirstOutCallbackGroupHandle",
//            "TimerBase ready callback called");

           ready_timers.emplace_back(ReadyEntity::ReadyTimerWithExecutedCallback{weak_ptr,
               executed_callback});

           check_move_to_ready();
         };
}

std::function<void(size_t)> PriorityCallbackGroupHandle::get_ready_callback_for_entity(
  const rclcpp::ClientBase::WeakPtr & entity)
{
  return [weak_ptr = entity, this](size_t nr_msg) {
           std::lock_guard l(ready_mutex);

//                 RCUTILS_LOG_ERROR_NAMED("rclcpp", "subscriber got data");
           for (size_t i = 0; i < nr_msg; i++) {
             ready_clients.emplace_back(weak_ptr);
           }

           check_move_to_ready();
         };
}

std::function<void(size_t)> PriorityCallbackGroupHandle::get_ready_callback_for_entity(
  const rclcpp::ServiceBase::WeakPtr & entity)
{
  return [weak_ptr = entity, this](size_t nr_msg) {
           std::lock_guard l(ready_mutex);

//                 RCUTILS_LOG_ERROR_NAMED("rclcpp", "subscriber got data");
           for (size_t i = 0; i < nr_msg; i++) {
             ready_services.emplace_back(weak_ptr);
           }

           check_move_to_ready();
         };
}

std::function<void(size_t,
  int)> PriorityCallbackGroupHandle::get_ready_callback_for_entity(
  const rclcpp::Waitable::WeakPtr & entity)
{
  return [weak_ptr = entity, this](size_t nr_msg, int event_type) {
           std::lock_guard l(ready_mutex);

//         RCUTILS_LOG_ERROR_NAMED("rclcpp", "Waitable got data");
           for (size_t i = 0; i < nr_msg; i++) {
             ready_waitables.emplace_back(CBGScheduler::WaitableWithEventType({weak_ptr,
                 event_type}));
           }

           check_move_to_ready();
         };
}
std::function<void(size_t)> PriorityCallbackGroupHandle::get_ready_callback_for_entity(
  const CBGScheduler::CallbackEventType & entity)
{
  return [weak_ptr = entity, this](size_t nr_msg) {
           std::lock_guard l(ready_mutex);

//                 RCUTILS_LOG_ERROR_NAMED("rclcpp", "subscriber got data");
           for (size_t i = 0; i < nr_msg; i++) {
             ready_calls.emplace_back(weak_ptr);
           }

           check_move_to_ready();
         };
}

std::optional<CBGScheduler::ExecutableEntity> PriorityCallbackGroupHandle::get_next_ready_entity(
  enum Priorities for_priority)
{
  switch(for_priority) {
    case Calls:
      return get_next_ready_entity(ready_calls);
      break;
    case Client:
      return get_next_ready_entity(ready_clients);
      break;
    case Service:
      return get_next_ready_entity(ready_services);
      break;
    case Subscription:
      return get_next_ready_entity(ready_subscriptions);
      break;
    case Timer:
      return get_next_ready_entity(ready_timers);
      break;
    case Waitable:
      return get_next_ready_entity(ready_waitables);
      break;
  }

  return std::nullopt;
}

std::optional<CBGScheduler::ExecutableEntity> PriorityCallbackGroupHandle::get_next_ready_entity(
  GlobalEventIdProvider::MonotonicId max_id, enum Priorities for_priority)
{
  switch(for_priority) {
    case Calls:
      return get_next_ready_entity(max_id, ready_calls);
      break;
    case Client:
      return get_next_ready_entity(max_id, ready_clients);
      break;
    case Service:
      return get_next_ready_entity(max_id, ready_services);
      break;
    case Subscription:
      return get_next_ready_entity(max_id, ready_subscriptions);
      break;
    case Timer:
      return get_next_ready_entity(max_id, ready_timers);
      break;
    case Waitable:
      return get_next_ready_entity(max_id, ready_waitables);
      break;
  }

  return std::nullopt;
}

std::optional<CBGScheduler::ExecutableEntity> PriorityCallbackGroupHandle::get_next_ready_entity(
  std::deque<ReadyEntity> & queue)
{
//     RCUTILS_LOG_ERROR_NAMED("PriorityCallbackGroupHandle", "get_next_ready_entity called");
  std::lock_guard l(ready_mutex);

  while(!queue.empty()) {
    auto & first = queue.front();

    std::function<void()> exec_fun = first.get_execute_function();
    queue.pop_front();
    if(!exec_fun) {
//         RCUTILS_LOG_ERROR_NAMED("PriorityCallbackGroupHandle",
//         "found ready entity, but func was empty");

      // was deleted, or in case of timer was canceled
      continue;
    }

    return CBGScheduler::ExecutableEntity{exec_fun, this};
  }

  mark_as_skiped();

  return std::nullopt;
}

std::optional<CBGScheduler::ExecutableEntity> PriorityCallbackGroupHandle::get_next_ready_entity(
  GlobalEventIdProvider::MonotonicId max_id, std::deque<ReadyEntity> & queue)
{
  std::lock_guard l(ready_mutex);

  while(!queue.empty()) {
    auto & first = queue.front();
    if(first.id > max_id) {
//       RCUTILS_LOG_ERROR_NAMED("PriorityCallbackGroupHandle",
//                               ("had work, but Id was to small " + std::to_string(first.id)
//                               + " max id " + std::to_string(max_id)).c_str());
      return std::nullopt;
    }

    std::function<void()> exec_fun = first.get_execute_function();
    queue.pop_front();
    if(!exec_fun) {
//       RCUTILS_LOG_ERROR_NAMED("PriorityCallbackGroupHandle",
//                               ("found entity, but got no exec_fun " +
//                                 std::to_string(max_id)).c_str());

      // was deleted, or in case of timer was canceled
      continue;
    }

    return CBGScheduler::ExecutableEntity{exec_fun, this};
  }

  mark_as_skiped();

//   RCUTILS_LOG_ERROR_NAMED("PriorityCallbackGroupHandle",
//                           ("no ready_entities max id " +
//                           std::to_string(max_id)).c_str());

  return std::nullopt;
}

std::unique_ptr<PriorityScheduler::CallbackGroupHandle> PriorityScheduler::
get_handle_for_callback_group(const rclcpp::CallbackGroup::SharedPtr &/*callback_group*/)
{
  return std::make_unique<PriorityCallbackGroupHandle>(*this);
}

std::optional<PriorityScheduler::ExecutableEntity> PriorityScheduler::get_next_ready_entity()
{
  std::lock_guard l(ready_callback_groups_mutex);

  for (size_t i = PriorityCallbackGroupHandle::Priorities::Calls;
    i <= PriorityCallbackGroupHandle::Priorities::Waitable; i++)
  {
    PriorityCallbackGroupHandle::Priorities cur_prio(
      static_cast<PriorityCallbackGroupHandle::Priorities>(
        i ) );

    for (auto it = ready_callback_groups.begin(); it != ready_callback_groups.end(); it++) {
      PriorityCallbackGroupHandle *ready_cbg(static_cast<PriorityCallbackGroupHandle *>(*it));
      std::optional<PriorityScheduler::ExecutableEntity> ret =
        ready_cbg->get_next_ready_entity(cur_prio);
      if(ret) {
        ready_callback_groups.erase(it);
        return ret;
      }
    }
  }

  return std::nullopt;
}

std::optional<PriorityScheduler::ExecutableEntity> PriorityScheduler::get_next_ready_entity(
  GlobalEventIdProvider::MonotonicId max_id)
{
  std::lock_guard l(ready_callback_groups_mutex);

  // as, we remove an reappend ready callback_groups during execution,
  // the first ready cbg may not contain the lowest id. Therefore we
  // need to search the whole deque
  for (size_t i = PriorityCallbackGroupHandle::Priorities::Calls;
    i <= PriorityCallbackGroupHandle::Priorities::Waitable; i++)
  {
    PriorityCallbackGroupHandle::Priorities cur_prio(
      static_cast<PriorityCallbackGroupHandle::Priorities>(
        i ) );

    for (auto it = ready_callback_groups.begin(); it != ready_callback_groups.end(); it++) {
      PriorityCallbackGroupHandle *ready_cbg(static_cast<PriorityCallbackGroupHandle *>(*it));
      std::optional<PriorityScheduler::ExecutableEntity> ret =
        ready_cbg->get_next_ready_entity(max_id, cur_prio);
      if(ret) {
        ready_callback_groups.erase(it);
        return ret;
      }
    }
  }

  return std::nullopt;
}
}  // namespace executors
}  // namespace rclcpp
