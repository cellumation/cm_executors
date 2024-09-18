#pragma once
#include <stdint.h>
#include <atomic>

namespace rclcpp
{
namespace executors
{
class GlobalEventIdProvider
{
  static std::atomic<uint64_t> last_event_id;

public:

  using MonotonicId = uint64_t;

  // Returns the last id, returnd by getNextId
  static uint64_t get_last_id()
  {
    return last_event_id;
  }

  // increases the Id by one and returns the Id
  static uint64_t get_next_id()
  {
    return (++last_event_id);
  }
};
}
}
