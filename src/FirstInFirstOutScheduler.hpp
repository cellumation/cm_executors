#pragma once
#include <cm_executors/ready_entity.hpp>
#include <cm_executors/scheduler.hpp>
#include <cm_executors/global_event_id_provider.hpp>

#include <variant>
namespace rclcpp
{
namespace executors
{


struct FirstInFirstOutCallbackGroupHandle : public CBGScheduler::CallbackGroupHandle
{
public:

    FirstInFirstOutCallbackGroupHandle(CBGScheduler &scheduler) : CallbackGroupHandle(scheduler) {};
    ~FirstInFirstOutCallbackGroupHandle() final {} ;

  std::function<void(size_t)> get_ready_callback_for_entity(const rclcpp::SubscriptionBase::WeakPtr & entity) final;
  std::function<void(std::function<void()> executed_callback)> get_ready_callback_for_entity(const rclcpp::TimerBase::WeakPtr & entity) final;
  std::function<void(size_t)> get_ready_callback_for_entity(const rclcpp::ClientBase::WeakPtr & entity) final;
  std::function<void(size_t)> get_ready_callback_for_entity(const rclcpp::ServiceBase::WeakPtr & entity) final;
  std::function<void(size_t, int)> get_ready_callback_for_entity(const rclcpp::Waitable::WeakPtr & entity) final;
  std::function<void(size_t)> get_ready_callback_for_entity(const CBGScheduler::CallbackEventType & entity) final;

  std::optional<CBGScheduler::ExecutableEntity> get_next_ready_entity();
  std::optional<CBGScheduler::ExecutableEntity> get_next_ready_entity(GlobalEventIdProvider::MonotonicId max_id);

  bool has_ready_entities() const final
  {
    return !ready_entities.empty();
  }

private:


  std::deque<ReadyEntity> ready_entities;
};

class FirstInFirstOutScheduler : public CBGScheduler
{
public:

    std::optional<ExecutableEntity> get_next_ready_entity() final;
    std::optional<ExecutableEntity> get_next_ready_entity(GlobalEventIdProvider::MonotonicId max_id) final;

private:

    std::unique_ptr<CallbackGroupHandle> get_handle_for_callback_group(const rclcpp::CallbackGroup::SharedPtr &callback_group) final;

    std::vector<std::unique_ptr<FirstInFirstOutCallbackGroupHandle>> callback_group_handles;
};

}
}
