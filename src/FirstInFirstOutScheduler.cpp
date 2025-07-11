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
#include "FirstInFirstOutScheduler.hpp"

namespace rclcpp
{
namespace executors
{

std::function<void(size_t)> FirstInFirstOutCallbackGroupHandle::get_ready_callback_for_entity(
  const rclcpp::SubscriptionBase::WeakPtr & entity)
{
  return [weak_ptr = entity, this](size_t nr_msg) {
           std::lock_guard l(ready_mutex);

//         RCUTILS_LOG_ERROR_NAMED("FirstInFirstOutCallbackGroupHandle", "subscriber got data");
           for (size_t i = 0; i < nr_msg; i++) {
             ready_entities.emplace_back(weak_ptr);
           }

           check_move_to_ready();
         };
}

std::function<void(std::function<void()> executed_callback)> FirstInFirstOutCallbackGroupHandle::
get_ready_callback_for_entity(const rclcpp::TimerBase::WeakPtr & entity)
{
  return [weak_ptr = entity, this](std::function<void()> executed_callback) {
           std::lock_guard l(ready_mutex);
//         RCUTILS_LOG_INFO_NAMED("FirstInFirstOutCallbackGroupHandle",
//            "TimerBase ready callback called");

           ready_entities.emplace_back(ReadyEntity::ReadyTimerWithExecutedCallback{weak_ptr,
               executed_callback});

           check_move_to_ready();
         };
}

std::function<void(size_t)> FirstInFirstOutCallbackGroupHandle::get_ready_callback_for_entity(
  const rclcpp::ClientBase::WeakPtr & entity)
{
  return [weak_ptr = entity, this](size_t nr_msg) {
           std::lock_guard l(ready_mutex);

//         RCUTILS_LOG_INFO_NAMED("FirstInFirstOutCallbackGroupHandle",
//            "ClientBase ready callback called");
           for (size_t i = 0; i < nr_msg; i++) {
             ready_entities.emplace_back(weak_ptr);
           }

           check_move_to_ready();
         };
}

std::function<void(size_t)> FirstInFirstOutCallbackGroupHandle::get_ready_callback_for_entity(
  const rclcpp::ServiceBase::WeakPtr & entity)
{
  return [weak_ptr = entity, this](size_t nr_msg) {
           std::lock_guard l(ready_mutex);

//         RCUTILS_LOG_INFO_NAMED("FirstInFirstOutCallbackGroupHandle",
//            "ServiceBase ready callback called");
           for (size_t i = 0; i < nr_msg; i++) {
             ready_entities.emplace_back(weak_ptr);
           }

           check_move_to_ready();
         };
}

std::function<void(size_t,
  int)> FirstInFirstOutCallbackGroupHandle::get_ready_callback_for_entity(
  const rclcpp::Waitable::WeakPtr & entity)
{
  return [weak_ptr = entity, this](size_t nr_msg, int event_type) {
           std::lock_guard l(ready_mutex);

//         RCUTILS_LOG_INFO_NAMED("FirstInFirstOutCallbackGroupHandle",
//            "Waitable ready callback called");
           for (size_t i = 0; i < nr_msg; i++) {
             ready_entities.emplace_back(CBGScheduler::WaitableWithEventType({weak_ptr,
                 event_type}));
           }

           check_move_to_ready();
         };
}
std::function<void(size_t)> FirstInFirstOutCallbackGroupHandle::get_ready_callback_for_entity(
  const CBGScheduler::CallbackEventType & entity)
{
  return [weak_ptr = entity, this](size_t nr_msg) {
           std::lock_guard l(ready_mutex);

//         RCUTILS_LOG_INFO_NAMED("FirstInFirstOutCallbackGroupHandle",
//            "CallbackEventType ready callback called");
           for (size_t i = 0; i < nr_msg; i++) {
             ready_entities.emplace_back(weak_ptr);
           }

           check_move_to_ready();
         };
}

std::optional<CBGScheduler::ExecutableEntity> FirstInFirstOutCallbackGroupHandle::
get_next_ready_entity()
{
//     RCUTILS_LOG_ERROR_NAMED("FirstInFirstOutCallbackGroupHandle",
//     "get_next_ready_entity called");
  std::lock_guard l(ready_mutex);

  while(!ready_entities.empty()) {
    auto & first = ready_entities.front();

    std::function<void()> exec_fun = first.get_execute_function();
    ready_entities.pop_front();
    if(!exec_fun) {
//             RCUTILS_LOG_ERROR_NAMED("FirstInFirstOutCallbackGroupHandle",
//         "found ready entity, but func was empty");
      // was deleted, or in case of timer was canceled
      continue;
    }

    return CBGScheduler::ExecutableEntity{exec_fun, this};
  }

  mark_as_skiped();

  return std::nullopt;
}

std::optional<CBGScheduler::ExecutableEntity> FirstInFirstOutCallbackGroupHandle::
get_next_ready_entity(GlobalEventIdProvider::MonotonicId max_id)
{
  std::lock_guard l(ready_mutex);

  while(!ready_entities.empty()) {
    auto & first = ready_entities.front();
    if(first.id > max_id) {
//             RCUTILS_LOG_ERROR_NAMED("FirstInFirstOutCallbackGroupHandle",
//         ("had work, but Id was to small " + std::to_string(first.id) +
//         " max id " + std::to_string(max_id)).c_str());
      return std::nullopt;
    }

    std::function<void()> exec_fun = first.get_execute_function();
    ready_entities.pop_front();
    if(!exec_fun) {
//             RCUTILS_LOG_ERROR_NAMED("FirstInFirstOutCallbackGroupHandle",
//         ("found entity, but got no exec_fun " + std::to_string(max_id)).c_str());

      // was deleted, or in case of timer was canceled
      continue;
    }

    return CBGScheduler::ExecutableEntity{exec_fun, this};
  }

  mark_as_skiped();

//   RCUTILS_LOG_ERROR_NAMED("FirstInFirstOutCallbackGroupHandle",
//   ("no ready_entities max id " + std::to_string(max_id)).c_str());

  return std::nullopt;
}

std::unique_ptr<FirstInFirstOutScheduler::CallbackGroupHandle> FirstInFirstOutScheduler::
get_handle_for_callback_group(const rclcpp::CallbackGroup::SharedPtr &/*callback_group*/)
{
  return std::make_unique<FirstInFirstOutCallbackGroupHandle>(*this);
}

std::optional<FirstInFirstOutScheduler::ExecutableEntity> FirstInFirstOutScheduler::
get_next_ready_entity()
{
  std::lock_guard l(ready_callback_groups_mutex);

  while(!ready_callback_groups.empty()) {
    FirstInFirstOutCallbackGroupHandle *ready_cbg =
      static_cast<FirstInFirstOutCallbackGroupHandle *>(ready_callback_groups.front());
    ready_callback_groups.pop_front();

    std::optional<FirstInFirstOutScheduler::ExecutableEntity> ret =
      ready_cbg->get_next_ready_entity();
    if(ret) {
      return ret;
    }
  }

  return std::nullopt;
}

std::optional<FirstInFirstOutScheduler::ExecutableEntity> FirstInFirstOutScheduler::
get_next_ready_entity(GlobalEventIdProvider::MonotonicId max_id)
{
  std::lock_guard l(ready_callback_groups_mutex);

  // as, we remove an reappend ready callback_groups during execution,
  // the first ready cbg may not contain the lowest id. Therefore we
  // need to search the whole deque
  for(auto it = ready_callback_groups.begin(); it != ready_callback_groups.end(); it++) {
    FirstInFirstOutCallbackGroupHandle *ready_cbg(
      static_cast<FirstInFirstOutCallbackGroupHandle *>(*it));
    std::optional<FirstInFirstOutScheduler::ExecutableEntity> ret =
      ready_cbg->get_next_ready_entity(max_id);
    if(ret) {
      ready_callback_groups.erase(it);
      return ret;
    }
  }

  return std::nullopt;
}
}  // namespace executors
}  // namespace rclcpp
