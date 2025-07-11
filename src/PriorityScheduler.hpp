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
#include <memory>
#include <vector>
#include <variant>

#include <cm_executors/ready_entity.hpp>
#include <cm_executors/events_cbg_executor.hpp>
#include <cm_executors/scheduler.hpp>
#include <cm_executors/global_event_id_provider.hpp>

namespace rclcpp
{
namespace executors
{


struct PriorityCallbackGroupHandle : public CBGScheduler::CallbackGroupHandle
{
public:
  explicit PriorityCallbackGroupHandle(CBGScheduler & scheduler)
  : CallbackGroupHandle(scheduler) {}
  ~PriorityCallbackGroupHandle() final {}

  std::function<void(size_t)> get_ready_callback_for_entity(
    const rclcpp::SubscriptionBase::WeakPtr & entity) final;
  std::function<void(std::function<void()> executed_callback)> get_ready_callback_for_entity(
    const rclcpp::TimerBase::WeakPtr & entity) final;
  std::function<void(size_t)> get_ready_callback_for_entity(
    const rclcpp::ClientBase::WeakPtr & entity) final;
  std::function<void(size_t)> get_ready_callback_for_entity(
    const rclcpp::ServiceBase::WeakPtr & entity) final;
  std::function<void(size_t,
    int)> get_ready_callback_for_entity(const rclcpp::Waitable::WeakPtr & entity) final;
  std::function<void(size_t)> get_ready_callback_for_entity(
    const CBGScheduler::CallbackEventType & entity) final;

  enum Priorities
  {
    Calls = 0,
    Timer,
    Subscription,
    Service,
    Client,
    Waitable
  };

  std::optional<CBGScheduler::ExecutableEntity> get_next_ready_entity(enum Priorities for_priority);
  std::optional<CBGScheduler::ExecutableEntity> get_next_ready_entity(
    GlobalEventIdProvider::MonotonicId max_id, enum Priorities for_priority);


  bool has_ready_entities() const final
  {
    return ready_calls.empty() ||
           ready_subscriptions.empty() ||
           ready_timers.empty() ||
           ready_clients.empty() ||
           ready_services.empty() ||
           ready_waitables.empty();
  }

private:
  std::optional<CBGScheduler::ExecutableEntity> get_next_ready_entity(
    std::deque<ReadyEntity> & queue);
  std::optional<CBGScheduler::ExecutableEntity> get_next_ready_entity(
    GlobalEventIdProvider::MonotonicId max_id, std::deque<ReadyEntity> & queue);

  std::deque<ReadyEntity> ready_timers;
  std::deque<ReadyEntity> ready_subscriptions;
  std::deque<ReadyEntity> ready_services;
  std::deque<ReadyEntity> ready_clients;
  std::deque<ReadyEntity> ready_waitables;
  std::deque<ReadyEntity> ready_calls;
};

class PriorityScheduler : public CBGScheduler
{
public:
  std::optional<ExecutableEntity> get_next_ready_entity() final;
  std::optional<ExecutableEntity> get_next_ready_entity(
    GlobalEventIdProvider::MonotonicId max_id) final;

private:
  std::unique_ptr<CallbackGroupHandle> get_handle_for_callback_group(
    const rclcpp::CallbackGroup::SharedPtr & callback_group) final;

  std::vector<std::unique_ptr<PriorityCallbackGroupHandle>> callback_group_handles;
};
}  // namespace executors
}  // namespace rclcpp
