#pragma once

#include <atomic>
#include <chrono>

#if defined(_MSC_VER) && !defined(__clang__)
#include <cstdlib>
#include <intrin.h>

#define __builtin_bswap16 _byteswap_ushort
#define __builtin_bswap32 _byteswap_ulong
#define __builtin_bswap64 _byteswap_uint64

inline unsigned int __builtin_clz(unsigned int value) {
  unsigned long index = 0;
  if (_BitScanReverse(&index, value) == 0) {
    return 32;
  }
  return 31u - index;
}

inline unsigned int __builtin_clzll(unsigned long long value) {
  unsigned long index = 0;
  if (_BitScanReverse64(&index, value) == 0) {
    return 64;
  }
  return 63u - index;
}

inline unsigned int __builtin_rotateleft32(unsigned int value, int shift) {
  return _rotl(value, shift);
}

inline unsigned long long __builtin_rotateleft64(unsigned long long value, int shift) {
  return _rotl64(value, shift);
}

[[noreturn]] inline void __builtin_trap() {
  __debugbreak();
  std::abort();
}

inline bool __sync_bool_compare_and_swap(uint32_t* ptr, uint32_t expected, uint32_t desired) {
  return _InterlockedCompareExchange(reinterpret_cast<volatile long*>(ptr),
                                     static_cast<long>(desired),
                                     static_cast<long>(expected)) == static_cast<long>(expected);
}

inline bool __sync_bool_compare_and_swap(uint64_t* ptr, uint64_t expected, uint64_t desired) {
  return _InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(ptr),
                                       static_cast<long long>(desired),
                                       static_cast<long long>(expected)) == static_cast<long long>(expected);
}

struct __uint128_t {
  uint64_t low;
  uint64_t high;

  __uint128_t(uint64_t value) : low(value), high(0) {}
  __uint128_t(uint64_t low_value, uint64_t high_value) : low(low_value), high(high_value) {}

  explicit operator uint64_t() const {
    return low;
  }
};

inline __uint128_t operator*(const __uint128_t& lhs, const __uint128_t& rhs) {
  uint64_t high = 0;
  const uint64_t low = _umul128(lhs.low, rhs.low, &high);
  return __uint128_t(low, high);
}

inline __uint128_t operator>>(const __uint128_t& value, unsigned int shift) {
  if (shift == 0) {
    return value;
  }
  if (shift < 64) {
    return __uint128_t((value.low >> shift) | (value.high << (64 - shift)), value.high >> shift);
  }
  if (shift < 128) {
    return __uint128_t(value.high >> (shift - 64), 0);
  }
  return __uint128_t(0, 0);
}
#endif

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
