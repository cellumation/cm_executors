#include <cm_executors/global_event_id_provider.hpp>

namespace rclcpp
{
namespace executors
{

std::atomic<uint64_t> GlobalEventIdProvider::last_event_id = 1;
}
}
