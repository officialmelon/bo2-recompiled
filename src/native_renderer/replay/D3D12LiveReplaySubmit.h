#pragma once

#include <cstdint>
#include <string>

#include "NativeRenderReplay.h"

#if defined(_WIN32)
struct ID3D12CommandQueue;
struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct ID3D12DescriptorHeap;
#endif

namespace bo2::native::replay {

#if defined(_WIN32)
struct D3D12LiveReplaySession;

struct D3D12LiveSubmitBinding {
  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12GraphicsCommandList *command_list = nullptr;
  ID3D12Resource *color_target = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  uint32_t rtv_descriptor_index = 0;
  uint32_t width = 1280;
  uint32_t height = 720;
  D3D12LiveReplaySession *session = nullptr;
  uint64_t submitted_draws = 0;
  uint64_t shader_pair_count = 0;
  uint64_t pso_entries = 0;
  uint64_t pso_cache_misses = 0;
  uint64_t pso_cache_hits = 0;
  uint64_t diagnostic_pipelines = 0;
  uint64_t input_layout_variants = 0;
  uint64_t scene_candidate_draws = 0;
  uint64_t depth_only_draws = 0;
  uint64_t utility_draws = 0;
  bool presentable_frame = false;
  bool noop_utility_frame = false;
};

D3D12LiveReplaySession *CreateD3D12LiveReplaySession();
void DestroyD3D12LiveReplaySession(D3D12LiveReplaySession *session);

bool RunD3D12LiveFrameBackend(const ReplayCapture &capture,
                              const ReplayCliOptions &options,
                              D3D12LiveSubmitBinding &binding,
                              std::string &error);
#endif

}  // namespace bo2::native::replay
