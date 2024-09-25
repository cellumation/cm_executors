#pragma once
#include <cm_executors/ready_entity.hpp>
#include <cm_executors/events_cbg_executor.hpp>
#include <cm_executors/scheduler.hpp>
#include <cm_executors/global_event_id_provider.hpp>

#include <variant>
namespace rclcpp
{
namespace executors
{


struct PriorityCallbackGroupHandle : public CBGScheduler::CallbackGroupHandle
{
public:

    PriorityCallbackGroupHandle(CBGScheduler &scheduler) : CallbackGroupHandle(scheduler) {};
    ~PriorityCallbackGroupHandle() final {} ;

  std::function<void(size_t)> get_ready_callback_for_entity(const rclcpp::SubscriptionBase::WeakPtr & entity) final;
  std::function<void(std::function<void()> executed_callback)> get_ready_callback_for_entity(const rclcpp::TimerBase::WeakPtr & entity) final;
  std::function<void(size_t)> get_ready_callback_for_entity(const rclcpp::ClientBase::WeakPtr & entity) final;
  std::function<void(size_t)> get_ready_callback_for_entity(const rclcpp::ServiceBase::WeakPtr & entity) final;
  std::function<void(size_t, int)> get_ready_callback_for_entity(const rclcpp::Waitable::WeakPtr & entity) final;
  std::function<void(size_t)> get_ready_callback_for_entity(const CBGScheduler::CallbackEventType & entity) final;

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
  std::optional<CBGScheduler::ExecutableEntity> get_next_ready_entity(GlobalEventIdProvider::MonotonicId max_id, enum Priorities for_priority);


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

  std::optional<CBGScheduler::ExecutableEntity> get_next_ready_entity(std::deque<ReadyEntity> &queue);
  std::optional<CBGScheduler::ExecutableEntity> get_next_ready_entity(GlobalEventIdProvider::MonotonicId max_id, std::deque<ReadyEntity> &queue);

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
    std::optional<ExecutableEntity> get_next_ready_entity(GlobalEventIdProvider::MonotonicId max_id) final;

private:

    std::unique_ptr<CallbackGroupHandle> get_handle_for_callback_group(const rclcpp::CallbackGroup::SharedPtr &callback_group) final;

    std::vector<std::unique_ptr<PriorityCallbackGroupHandle>> callback_group_handles;
};

}
}

