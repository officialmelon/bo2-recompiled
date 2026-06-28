#pragma once

#include <atomic>
#include <chrono>
#include <rex/chrono/chrono.h>

namespace std::chrono {

template <>
struct clock_time_conversion<::rex::chrono::WinSystemClock, std::chrono::steady_clock> {
  template <typename Duration>
  ::rex::chrono::WinSystemClock::time_point operator()(
      const std::chrono::time_point<std::chrono::steady_clock, Duration>& time) const {
    std::atomic_thread_fence(std::memory_order_acq_rel);
    const auto win_now = ::rex::chrono::WinSystemClock::now();
    const auto steady_now = std::chrono::steady_clock::now();
    return win_now +
           std::chrono::duration_cast<::rex::chrono::WinSystemClock::duration>(time - steady_now);
  }
};

template <>
struct clock_time_conversion<std::chrono::steady_clock, ::rex::chrono::WinSystemClock> {
  template <typename Duration>
  std::chrono::steady_clock::time_point operator()(
      const std::chrono::time_point<::rex::chrono::WinSystemClock, Duration>& time) const {
    std::atomic_thread_fence(std::memory_order_acq_rel);
    const auto win_now = ::rex::chrono::WinSystemClock::now();
    const auto steady_now = std::chrono::steady_clock::now();
    return steady_now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            time - win_now);
  }
};

}  // namespace std::chrono
