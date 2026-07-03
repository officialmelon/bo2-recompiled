#pragma once

#include <array>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "../NativeRenderCaptureWriter.h"
#include "../RendererBackend.h"
#include "../replay/D3D12LiveReplaySubmit.h"
#include "D3D12LiveFrameBuilder.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#endif

namespace bo2::native {

class D3D12LiveRendererBackend final : public RendererBackend {
 public:
  RendererBackendKind Kind() const override { return RendererBackendKind::D3D12Live; }
  bool Initialize(const RendererConfig& config) override;
  void Shutdown() override;
  void BeginFrame(uint64_t frame_index) override;
  void SubmitVdSwap(uint64_t frame_index, const VdSwapInfo& swap) override;
  void SubmitCommandBufferSnapshot(uint64_t frame_index,
                                   const CommandBufferSnapshot& snapshot) override;
  void SubmitDrawPacketCandidate(const DrawPacketCandidateInfo& draw) override;
  void SubmitShaderRecordProbe(const ShaderRecordProbeInfo& probe) override;
  void SubmitPM4Packet(const PM4PacketInfo& packet) override;
  void SubmitPM4Draw(const PM4DrawInfo& draw) override;
  void SubmitPM4Shader(const PM4ShaderInfo& shader) override;
  void SubmitPM4Constants(const PM4ConstantInfo& constants) override;
  void SubmitPM4Swap(const PM4SwapInfo& swap) override;
  void SubmitRenderCommand(const RenderCommand& command) override;
  void EndFrame(uint64_t frame_index) override;
  std::string_view LastError() const override { return last_error_; }

 private:
  struct FrameStats {
    uint64_t packets = 0;
    uint64_t draws = 0;
    uint64_t shaders = 0;
    uint64_t constants = 0;
    uint64_t swaps = 0;
  };
  struct LiveSubmitStats {
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
    uint64_t skipped_draws = 0;
    uint64_t elided_noop_draws = 0;
    uint32_t candidate_presented_guest_color_base = 0;
    uint32_t selected_presented_guest_color_base = 0;
    std::vector<std::pair<std::string, uint64_t>> unsupported_reasons;
    bool presentable_frame = false;
    bool noop_utility_frame = false;
    bool retained_color_ready = false;
    bool copied_retained_frame = false;
    bool rejected_presented_guest_color_switch = false;
  };
  struct LiveDiagnostics {
    uint64_t frames_attempted = 0;
    uint64_t frames_submitted = 0;
    uint64_t frames_presentable = 0;
    uint64_t frames_retained_copy = 0;
    uint64_t frames_retained_blocked = 0;
    uint64_t frames_not_presentable = 0;
    uint64_t skipped_draws = 0;
    uint64_t elided_noop_draws = 0;
    uint64_t pso_cache_misses = 0;
    uint64_t pso_cache_hits = 0;
    uint64_t diagnostic_pipelines = 0;
    std::map<std::string, uint64_t> unsupported_reasons;
  };

  FrameStats& ActiveStats();
  D3D12LiveFrameBuilder& ActiveFrameBuilder();
  bool BeginCommandFrame(uint64_t frame_index);
  void EndCommandFrame(uint64_t frame_index, bool execute);
  bool WaitForFenceValue(uint64_t fence_value);
  bool WaitForGpu();
  bool EnsureSwapChain(uint32_t width, uint32_t height);
  bool EnsureNativeWindow(uint32_t width, uint32_t height);
  void NativeWindowThreadMain(uint32_t width, uint32_t height);
  bool RefreshSwapChainBackBuffer(std::string& error);
  bool SubmitLiveFrame(uint64_t frame_index);
  void RecordLiveSubmitDiagnostics(uint64_t frame_index,
                                   bool attempted_submit,
                                   bool submit_success,
                                   bool present_native_frame);
  void MaybeLogLiveDiagnostics(uint64_t frame_index,
                               bool force = false) const;
  void PumpNativeWindowMessages();
  replay::ReplayCliOptions BuildReplayOptions() const;

  bool verbose_ = true;
  bool skip_unsupported_draws_ = true;
  bool allow_diagnostic_shader_ = false;
  std::string app_name_;
  std::string live_pipeline_;
  std::string last_error_;
  NativeRenderCaptureWriter capture_;
  D3D12LiveFrameBuilder frame_builder_;
  D3D12LiveFrameBuilder pending_frame_builder_;
  FrameStats frame_stats_;
  FrameStats pending_stats_;
  LiveSubmitStats last_submit_stats_;
  LiveDiagnostics live_diagnostics_;
  bool in_frame_ = false;
  uint64_t frame_pending_draws_ = 0;
  uint32_t frame_width_ = 1280;
  uint32_t frame_height_ = 720;
  uint64_t submitted_frames_ = 0;
  uint64_t failed_frames_ = 0;
  std::filesystem::path shader_cache_root_;
  std::filesystem::path shader_override_root_;

#if defined(_WIN32)
  static constexpr uint32_t kLiveCommandFrameCount = 3;

  struct CommandFrameContext {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
    uint64_t fence_value = 0;
    bool open = false;
  };

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue_;
  bool debug_layer_enabled_ = false;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
  std::array<CommandFrameContext, kLiveCommandFrameCount> command_frames_;
  CommandFrameContext* active_command_frame_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  Microsoft::WRL::ComPtr<IDXGIFactory4> dxgi_factory_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  Microsoft::WRL::ComPtr<ID3D12Resource> color_target_;
  replay::D3D12LiveReplaySession* replay_session_ = nullptr;
  HWND window_handle_ = nullptr;
  bool owns_window_ = false;
  std::thread window_thread_;
  std::mutex window_mutex_;
  std::condition_variable window_cv_;
  std::atomic<bool> window_thread_stop_ = false;
  bool window_ready_ = false;
  bool window_failed_ = false;
  HANDLE fence_event_ = nullptr;
  uint64_t fence_value_ = 0;
  uint32_t next_command_frame_ = 0;
  uint32_t swap_width_ = 0;
  uint32_t swap_height_ = 0;
  bool swapchain_ready_ = false;
#endif
};

}  // namespace bo2::native
