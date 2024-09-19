#pragma once
#include <cm_executors/scheduler.hpp>
#include <cm_executors/global_event_id_provider.hpp>
#include <deque>
#include <cm_executors/events_cbg_executor.hpp>
#include "timer_manager.hpp"

namespace rclcpp
{
namespace executors
{

struct ReadyEntity
{
    struct ReadyTimerWithExecutedCallback
    {
        const rclcpp::TimerBase::WeakPtr timer_ptr;
        // must be called by the after executing the timer callback
        std::function<void()> timer_was_executed;

        bool expired() const
        {
            return timer_ptr.expired();
        }
    };

    std::variant<rclcpp::SubscriptionBase::WeakPtr, ReadyTimerWithExecutedCallback, rclcpp::ServiceBase::WeakPtr, rclcpp::ClientBase::WeakPtr, CBGScheduler::WaitableWithEventType, CBGScheduler::CallbackEventType> entity;

    ReadyEntity(const rclcpp::SubscriptionBase::WeakPtr ptr) : entity(ptr), id(GlobalEventIdProvider::get_next_id()) {};
    ReadyEntity(const ReadyTimerWithExecutedCallback &timer) : entity(timer), id(GlobalEventIdProvider::get_next_id()) {};
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
            } else if constexpr (std::is_same_v<T, ReadyTimerWithExecutedCallback>)
            {
                auto shr_ptr = entity.timer_ptr.lock();
                if (!shr_ptr) {
                    return std::function<void()>();
                }
                auto data = shr_ptr->call();
                if (!data) {
                    // timer was cancelled, skip it.
                    return std::function<void()>();
                }

                return [shr_ptr = std::move(shr_ptr), data = std::move(data), timer_executed_cb = entity.timer_was_executed]() {
                    rclcpp::executors::EventsCBGExecutor::execute_timer(shr_ptr, data);

                    // readd the timer to the timers manager
                    timer_executed_cb();
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
//                 RCUTILS_LOG_INFO("Requested execution function for waitable");
                auto shr_ptr_in = entity.waitable.lock();
                if (!shr_ptr_in) {
                    return std::function<void()>();
                }
                auto data_in = shr_ptr_in->take_data_by_entity_id(entity.internal_event_type);

                return [shr_ptr = std::move(shr_ptr_in), data = std::move(data_in)]() {
//                     RCUTILS_LOG_INFO("Before execute of waitable");
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
}
}
