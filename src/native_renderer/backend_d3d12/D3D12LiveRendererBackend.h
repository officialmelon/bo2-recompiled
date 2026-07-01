#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "../NativeRenderCaptureWriter.h"
#include "../RendererBackend.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
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

  void SetUnsupportedLiveRenderErrorOnce();
  FrameStats& ActiveStats();
  bool BeginCommandFrame(uint64_t frame_index);
  void EndCommandFrame(uint64_t frame_index);
  bool WaitForGpu();

  bool verbose_ = true;
  std::string app_name_;
  std::string last_error_;
  NativeRenderCaptureWriter capture_;
  FrameStats frame_stats_;
  FrameStats pending_stats_;
  bool in_frame_ = false;
  bool reported_live_render_gap_ = false;

#if defined(_WIN32)
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  uint64_t fence_value_ = 0;
  bool command_frame_open_ = false;
#endif
};

}  // namespace bo2::native
