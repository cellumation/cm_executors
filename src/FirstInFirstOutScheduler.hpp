#pragma once
#include <cm_executors/events_cbg_executor.hpp>
#include <cm_executors/scheduler.hpp>
#include <cm_executors/global_event_id_provider.hpp>

#include <variant>
namespace rclcpp
{
namespace executors
{

struct ReadyEntity
{
    std::variant<rclcpp::SubscriptionBase::WeakPtr, rclcpp::TimerBase::WeakPtr, rclcpp::ServiceBase::WeakPtr, rclcpp::ClientBase::WeakPtr, CBGScheduler::WaitableWithEventType, CBGScheduler::CallbackEventType> entity;

    ReadyEntity(const rclcpp::SubscriptionBase::WeakPtr ptr) : entity(ptr), id(GlobalEventIdProvider::get_next_id()) {};
    ReadyEntity(const rclcpp::TimerBase::WeakPtr ptr) : entity(ptr), id(GlobalEventIdProvider::get_next_id()) {};
    ReadyEntity(const rclcpp::ServiceBase::WeakPtr ptr) : entity(ptr), id(GlobalEventIdProvider::get_next_id()) {};
    ReadyEntity(const rclcpp::ClientBase::WeakPtr ptr) : entity(ptr), id(GlobalEventIdProvider::get_next_id()) {};
    ReadyEntity(const CBGScheduler::WaitableWithEventType &ev) : entity(ev), id(GlobalEventIdProvider::get_next_id()) {};
    ReadyEntity(const CBGScheduler::CallbackEventType &ev) : entity(ev), id(GlobalEventIdProvider::get_next_id()) {};

    std::function<void()> get_execute_function() const
    {
       return std::visit([](auto &&entity) -> std::function<void()> {
            using T = std::decay_t<decltype(entity)>;
            if constexpr (std::is_same_v<T, rclcpp::SubscriptionBase::WeakPtr>)
            {
                rclcpp::SubscriptionBase::SharedPtr shr_ptr = entity.lock();
                if (!shr_ptr) {
                    return std::function<void()>();
                }
                return [shr_ptr = std::move(shr_ptr)]() {
                    rclcpp::executors::EventsCBGExecutor::execute_subscription(shr_ptr);
                };
            } else if constexpr (std::is_same_v<T, rclcpp::TimerBase::WeakPtr>)
            {
                auto shr_ptr = entity.lock();
                if (!shr_ptr) {
                    return std::function<void()>();
                }
                auto data = shr_ptr->call();
                if (!data) {
                    // timer was cancelled, skip it.
                    return std::function<void()>();
                }

                return [shr_ptr = std::move(shr_ptr), data = std::move(data)]() {
                    rclcpp::executors::EventsCBGExecutor::execute_timer(shr_ptr, data);
                };
            } else if constexpr (std::is_same_v<T, rclcpp::ServiceBase::WeakPtr>)
            {
                auto shr_ptr = entity.lock();
                if (!shr_ptr) {
                    return std::function<void()>();
                }
                return [shr_ptr = std::move(shr_ptr)]() {
                    rclcpp::executors::EventsCBGExecutor::execute_service(shr_ptr);
                };
            }
             else if constexpr (std::is_same_v<T, rclcpp::ClientBase::WeakPtr>)
            {
                auto shr_ptr = entity.lock();
                if (!shr_ptr) {
                    return std::function<void()>();
                }
                return [shr_ptr = std::move(shr_ptr)]() {
                    rclcpp::executors::EventsCBGExecutor::execute_client(shr_ptr);
                };
            }else if constexpr (std::is_same_v<T, CBGScheduler::WaitableWithEventType>)
            {
                RCUTILS_LOG_INFO("Requested execution function for waitable");
                auto shr_ptr_in = entity.waitable.lock();
                if (!shr_ptr_in) {
                    return std::function<void()>();
                }
                auto data_in = shr_ptr_in->take_data_by_entity_id(entity.internal_event_type);

                return [shr_ptr = std::move(shr_ptr_in), data = std::move(data_in)]() {
                    RCUTILS_LOG_INFO("Before execute of waitable");
                    shr_ptr->execute(data);
                };
            } else if constexpr (std::is_same_v<T, CBGScheduler::CallbackEventType>)
            {
                return entity.callback;
            }
        }, entity);
    };

    GlobalEventIdProvider::MonotonicId id;

    /**
     * Returns true if the event has expired / does not need to be executed any more
     */
    bool expired() const
    {
        return std::visit([](const auto &entity) {return entity.expired();}, entity);
    }
};

struct FirstInFirstOutCallbackGroupHandle : public CBGScheduler::CallbackGroupHandle
{
public:

    FirstInFirstOutCallbackGroupHandle(CBGScheduler &scheduler) : CallbackGroupHandle(scheduler) {};
    ~FirstInFirstOutCallbackGroupHandle() final {} ;

  std::function<void(size_t)> get_ready_callback_for_entity(const rclcpp::SubscriptionBase::WeakPtr & entity) final;
  std::function<void()> get_ready_callback_for_entity(const rclcpp::TimerBase::WeakPtr & entity) final;
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
//
//   bool has_ready_entities(GlobalEventIdProvider::MonotonicId max_id) const
//   {
//       if(ready_entities.empty())
//       {
//           return false;
//       }
//
//       if(ready_entities.front().id > max_id)
//       {
//           return false;
//       }
//
//       return true;
//   }

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
