#include "D3D12LiveRendererBackend.h"

#include <rex/logging.h>

#include "../DebugRenderLog.h"

namespace bo2::native {

bool D3D12LiveRendererBackend::Initialize(const RendererConfig &config) {
  verbose_ = config.verbose;
  app_name_ = config.app_name;
  last_error_.clear();
  reported_live_render_gap_ = false;
  frame_stats_ = {};
  pending_stats_ = {};
  in_frame_ = false;

#if !defined(_WIN32)
  last_error_ = "native_d3d12 live backend requires Windows";
  return false;
#else
  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device_));
  if (FAILED(hr)) {
    last_error_ = "D3D12CreateDevice failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 backend init failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queue_desc.NodeMask = 0;
  hr = device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_));
  if (FAILED(hr)) {
    last_error_ = "CreateCommandQueue failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 command queue init failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }

  hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       IID_PPV_ARGS(&command_allocator_));
  if (FAILED(hr)) {
    last_error_ = "CreateCommandAllocator failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 command allocator init failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }

  hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  command_allocator_.Get(), nullptr,
                                  IID_PPV_ARGS(&command_list_));
  if (FAILED(hr)) {
    last_error_ = "CreateCommandList failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 command list init failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }
  hr = command_list_->Close();
  if (FAILED(hr)) {
    last_error_ = "Initial command list close failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 command list initial close failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }

  hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
  if (FAILED(hr)) {
    last_error_ = "CreateFence failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 fence init failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }
  fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!fence_event_) {
    last_error_ = "CreateEvent failed for native_d3d12 live fence";
    REXLOG_ERROR("BO2 native D3D12 fence event init failed");
    return false;
  }

  if (!capture_.Initialize(config)) {
    last_error_ = "failed to initialize native_d3d12 capture stream";
    return false;
  }

  REXLOG_INFO(
      "BO2 native D3D12 live backend initialized for {} "
      "(strict mode: no diagnostic fallback)",
      app_name_);
  return true;
#endif
}

void D3D12LiveRendererBackend::Shutdown() {
  WaitForGpu();
  capture_.Shutdown();
#if defined(_WIN32)
  if (fence_event_) {
    CloseHandle(fence_event_);
    fence_event_ = nullptr;
  }
  fence_.Reset();
  command_list_.Reset();
  command_allocator_.Reset();
  command_queue_.Reset();
  device_.Reset();
#endif
  REXLOG_INFO("BO2 native D3D12 live backend shutdown for {}", app_name_);
}

void D3D12LiveRendererBackend::BeginFrame(uint64_t frame_index) {
  frame_stats_ = pending_stats_;
  pending_stats_ = {};
  in_frame_ = true;
  capture_.WriteBeginFrame(frame_index);
  BeginCommandFrame(frame_index);
  if (verbose_ && ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO("BO2 native D3D12 frame {} begin", frame_index);
  }
}

void D3D12LiveRendererBackend::SubmitVdSwap(uint64_t frame_index,
                                            const VdSwapInfo &swap) {
  capture_.WriteVdSwap(frame_index, swap);
  if (verbose_ && ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO(
        "BO2 native D3D12 VdSwap frame={} cmd={:#010x} fetch={:#010x} "
        "frontbuffer_ptr={:#010x} width_ptr={:#010x} height_ptr={:#010x}",
        frame_index, swap.command_buffer, swap.fetch_constant,
        swap.frontbuffer_ptr, swap.width_ptr, swap.height_ptr);
  }
}

void D3D12LiveRendererBackend::SubmitCommandBufferSnapshot(
    uint64_t frame_index, const CommandBufferSnapshot &snapshot) {
  capture_.WriteCommandBufferSnapshot(frame_index, snapshot);
}

void D3D12LiveRendererBackend::SubmitDrawPacketCandidate(
    const DrawPacketCandidateInfo &draw) {
  capture_.WriteDrawPacketCandidate(draw);
}

void D3D12LiveRendererBackend::SubmitShaderRecordProbe(
    const ShaderRecordProbeInfo &probe) {
  capture_.WriteShaderRecordProbe(probe);
}

void D3D12LiveRendererBackend::SubmitPM4Packet(const PM4PacketInfo &packet) {
  ++ActiveStats().packets;
  capture_.WritePM4Packet(packet);
}

void D3D12LiveRendererBackend::SubmitPM4Draw(const PM4DrawInfo &draw) {
  ++ActiveStats().draws;
  capture_.WritePM4Draw(draw);
  SetUnsupportedLiveRenderErrorOnce();
}

void D3D12LiveRendererBackend::SubmitPM4Shader(const PM4ShaderInfo &shader) {
  ++ActiveStats().shaders;
  capture_.WritePM4Shader(shader);
}

void D3D12LiveRendererBackend::SubmitPM4Constants(
    const PM4ConstantInfo &constants) {
  ++ActiveStats().constants;
  capture_.WritePM4Constants(constants);
}

void D3D12LiveRendererBackend::SubmitPM4Swap(const PM4SwapInfo &swap) {
  ++ActiveStats().swaps;
  capture_.WritePM4Swap(swap);
}

void D3D12LiveRendererBackend::SubmitRenderCommand(
    const RenderCommand &command) {
  capture_.WriteRenderCommand(command);
}

void D3D12LiveRendererBackend::EndFrame(uint64_t frame_index) {
  capture_.WriteEndFrame(frame_index);
  EndCommandFrame(frame_index);
  if (!verbose_ && !reported_live_render_gap_) {
    in_frame_ = false;
    return;
  }

  if (ShouldLogHighFrequencyEvent(frame_index) || !reported_live_render_gap_) {
    REXLOG_WARN(
        "BO2 native D3D12 frame {} captured packets={} draws={} shaders={} "
        "constants={} swaps={} but live draw submission is still fail-closed "
        "until replay translation is factored into the live path",
        frame_index, frame_stats_.packets, frame_stats_.draws,
        frame_stats_.shaders, frame_stats_.constants, frame_stats_.swaps);
  }
  reported_live_render_gap_ = true;
  in_frame_ = false;
}

void D3D12LiveRendererBackend::SetUnsupportedLiveRenderErrorOnce() {
  if (!last_error_.empty()) {
    return;
  }
  last_error_ =
      "native_d3d12 live draw submission is not implemented yet; offline "
      "D3D12 replay remains the authoritative real renderer path";
}

D3D12LiveRendererBackend::FrameStats &D3D12LiveRendererBackend::ActiveStats() {
  return in_frame_ ? frame_stats_ : pending_stats_;
}

bool D3D12LiveRendererBackend::BeginCommandFrame(uint64_t frame_index) {
#if !defined(_WIN32)
  (void)frame_index;
  return false;
#else
  if (!command_allocator_ || !command_list_) {
    return false;
  }

  if (!WaitForGpu()) {
    return false;
  }

  HRESULT hr = command_allocator_->Reset();
  if (FAILED(hr)) {
    last_error_ = "Reset command allocator failed for native_d3d12 live frame";
    REXLOG_ERROR("BO2 native D3D12 command allocator reset failed frame={} "
                 "hr={:#010x}",
                 frame_index, static_cast<uint32_t>(hr));
    return false;
  }
  hr = command_list_->Reset(command_allocator_.Get(), nullptr);
  if (FAILED(hr)) {
    last_error_ = "Reset command list failed for native_d3d12 live frame";
    REXLOG_ERROR("BO2 native D3D12 command list reset failed frame={} "
                 "hr={:#010x}",
                 frame_index, static_cast<uint32_t>(hr));
    return false;
  }

  command_frame_open_ = true;
  return true;
#endif
}

void D3D12LiveRendererBackend::EndCommandFrame(uint64_t frame_index) {
#if !defined(_WIN32)
  (void)frame_index;
#else
  if (!command_frame_open_ || !command_list_ || !command_queue_) {
    return;
  }
  command_frame_open_ = false;

  HRESULT hr = command_list_->Close();
  if (FAILED(hr)) {
    last_error_ = "Close command list failed for native_d3d12 live frame";
    REXLOG_ERROR("BO2 native D3D12 command list close failed frame={} "
                 "hr={:#010x}",
                 frame_index, static_cast<uint32_t>(hr));
    return;
  }

  ID3D12CommandList *lists[] = {command_list_.Get()};
  command_queue_->ExecuteCommandLists(1, lists);
  WaitForGpu();
#endif
}

bool D3D12LiveRendererBackend::WaitForGpu() {
#if !defined(_WIN32)
  return false;
#else
  if (!command_queue_ || !fence_ || !fence_event_) {
    return false;
  }

  const uint64_t signal_value = ++fence_value_;
  HRESULT hr = command_queue_->Signal(fence_.Get(), signal_value);
  if (FAILED(hr)) {
    last_error_ = "Signal fence failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 fence signal failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }

  if (fence_->GetCompletedValue() >= signal_value) {
    return true;
  }

  hr = fence_->SetEventOnCompletion(signal_value, fence_event_);
  if (FAILED(hr)) {
    last_error_ = "SetEventOnCompletion failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 fence wait setup failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }
  WaitForSingleObject(fence_event_, INFINITE);
  return true;
#endif
}

}  // namespace bo2::native
