#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace bo2::native {

// Snapshot of live native-renderer activity published once per submitted
// frame by the D3D12 live backend and read by UI overlays. Copied under a
// mutex on both sides; the payload is small.
struct NativeRendererLiveStats {
  bool active = false;
  std::string backend;
  std::string pipeline;
  double fps = 0.0;
  double frame_time_ms = 0.0;
  uint64_t frames_submitted = 0;
  uint64_t frames_failed = 0;
  uint64_t draws_last_frame = 0;
  uint64_t skipped_draws_last_frame = 0;
  uint64_t shader_pairs = 0;
  uint64_t pso_entries = 0;
  uint64_t pso_cache_hits = 0;
  uint64_t pso_cache_misses = 0;
  uint64_t live_translated_shaders = 0;
  uint32_t presented_guest_color_base = 0;
};

namespace detail {
inline std::mutex g_live_stats_mutex;
inline NativeRendererLiveStats g_live_stats;
}  // namespace detail

inline void PublishNativeRendererLiveStats(const NativeRendererLiveStats& stats) {
  std::scoped_lock lock(detail::g_live_stats_mutex);
  detail::g_live_stats = stats;
}

inline NativeRendererLiveStats GetNativeRendererLiveStats() {
  std::scoped_lock lock(detail::g_live_stats_mutex);
  return detail::g_live_stats;
}

}  // namespace bo2::native
