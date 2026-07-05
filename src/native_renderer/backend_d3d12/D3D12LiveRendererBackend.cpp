#include "D3D12LiveRendererBackend.h"

#if defined(BO2_HAVE_XENOS_DXBC_TRANSLATOR)
#include <cstdio>
#include <fstream>
#include <sstream>
#include "../shader_translation/XenosDxbcTranslator.h"
#endif

#include <algorithm>
#include <chrono>
#include <sstream>

#include <rex/logging.h>

#include "../../../common/windows_dpi_awareness.h"
#include "../DebugRenderLog.h"
#include "../NativeRendererStats.h"

namespace bo2::native {
namespace {

#if defined(_WIN32)
LRESULT CALLBACK NativeD3D12WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                                       LPARAM lparam) {
  if (message == WM_CLOSE) {
    DestroyWindow(hwnd);
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

BOOL CALLBACK FindProcessWindowProc(HWND hwnd, LPARAM lparam) {
  DWORD window_pid = 0;
  GetWindowThreadProcessId(hwnd, &window_pid);
  if (window_pid != GetCurrentProcessId()) {
    return TRUE;
  }
  if (!IsWindowVisible(hwnd)) {
    return TRUE;
  }
  if (GetWindow(hwnd, GW_OWNER) != nullptr) {
    return TRUE;
  }
  auto* out = reinterpret_cast<HWND*>(lparam);
  *out = hwnd;
  return FALSE;
}

HWND FindProcessWindowHandle() {
  HWND found = nullptr;
  EnumWindows(FindProcessWindowProc, reinterpret_cast<LPARAM>(&found));
  return found;
}

std::wstring WidenAscii(std::string_view text) {
  std::wstring wide;
  wide.reserve(text.size());
  for (char ch : text) {
    wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
  }
  return wide;
}
#endif

}  // namespace

bool D3D12LiveRendererBackend::Initialize(const RendererConfig &config) {
  verbose_ = config.verbose;
  skip_unsupported_draws_ = config.skip_unsupported_draws;
  allow_diagnostic_shader_ = config.allow_live_diagnostic_shader;
  app_name_ = config.app_name;
  shader_cache_root_ = config.shader_cache_root;
  shader_override_root_ = config.shader_override_root;
  live_pipeline_ = config.live_pipeline;
  last_error_.clear();
  frame_stats_ = {};
  pending_stats_ = {};
  last_submit_stats_ = {};
  live_diagnostics_ = {};
  in_frame_ = false;
  submitted_frames_ = 0;
  failed_frames_ = 0;
  frame_pending_draws_ = 0;

#if !defined(_WIN32)
  last_error_ = "native_d3d12 live backend requires Windows";
  return false;
#else
  bo2::EnableProcessDpiAwareness();
  gpu_faulted_ = false;
  gpu_fault_frame_ = 0;

  // BO2_XENIA_DEBUG_LAYER=1 turns on the D3D12 debug layer for the live
  // device; validation messages are drained into the game log per frame.
  {
    char env_value[8]{};
    if (GetEnvironmentVariableA("BO2_XENIA_DEBUG_LAYER", env_value,
                                sizeof(env_value)) > 0 &&
        env_value[0] == '1') {
      Microsoft::WRL::ComPtr<ID3D12Debug> debug;
      if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        debug_layer_enabled_ = true;
        REXLOG_INFO("BO2 native D3D12 debug layer enabled for live device");
      }
    }
  }
  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device_));
  if (FAILED(hr)) {
    last_error_ = "D3D12CreateDevice failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 backend init failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }
  if (debug_layer_enabled_) {
    device_.As(&info_queue_);
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

  hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory_));
  if (FAILED(hr)) {
    last_error_ = "CreateDXGIFactory1 failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 DXGI factory init failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }

  for (uint32_t frame_slot = 0; frame_slot < kLiveCommandFrameCount;
       ++frame_slot) {
    CommandFrameContext& command_frame = command_frames_[frame_slot];
    hr = device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&command_frame.allocator));
    if (FAILED(hr)) {
      last_error_ =
          "CreateCommandAllocator failed for native_d3d12 live backend";
      REXLOG_ERROR(
          "BO2 native D3D12 command allocator init failed slot={} hr={:#010x}",
          frame_slot, static_cast<uint32_t>(hr));
      return false;
    }

    hr = device_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_frame.allocator.Get(),
        nullptr, IID_PPV_ARGS(&command_frame.command_list));
    if (FAILED(hr)) {
      last_error_ = "CreateCommandList failed for native_d3d12 live backend";
      REXLOG_ERROR(
          "BO2 native D3D12 command list init failed slot={} hr={:#010x}",
          frame_slot, static_cast<uint32_t>(hr));
      return false;
    }
    hr = command_frame.command_list->Close();
    if (FAILED(hr)) {
      last_error_ =
          "Initial command list close failed for native_d3d12 live backend";
      REXLOG_ERROR(
          "BO2 native D3D12 command list initial close failed slot={} "
          "hr={:#010x}",
          frame_slot, static_cast<uint32_t>(hr));
      return false;
    }
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

  replay_session_ = replay::CreateD3D12LiveReplaySession();

  if (!capture_.Initialize(config)) {
    last_error_ = "failed to initialize native_d3d12 capture stream";
    return false;
  }

  REXLOG_INFO(
      "BO2 native D3D12 live backend initialized for {} "
      "(shader_cache={} skip_unsupported={} diagnostic_shader={})",
      app_name_, shader_cache_root_.string(),
      skip_unsupported_draws_ ? "yes" : "no",
      allow_diagnostic_shader_ ? "yes" : "no");
  return true;
#endif
}

void D3D12LiveRendererBackend::Shutdown() {
#if defined(_WIN32)
  if (!gpu_faulted_) {
    WaitForGpu();
  }
#else
  WaitForGpu();
#endif
  capture_.Shutdown();
#if defined(_WIN32)
  if (replay_session_) {
    replay::DestroyD3D12LiveReplaySession(replay_session_);
    replay_session_ = nullptr;
  }
  color_target_.Reset();
  rtv_heap_.Reset();
  swap_chain_.Reset();
  dxgi_factory_.Reset();
  window_thread_stop_.store(true);
  if (window_handle_) {
    PostMessageW(window_handle_, WM_CLOSE, 0, 0);
  }
  if (window_thread_.joinable()) {
    window_thread_.join();
  }
  {
    std::scoped_lock lock(window_mutex_);
    window_handle_ = nullptr;
    owns_window_ = false;
    window_ready_ = false;
    window_failed_ = false;
  }
  if (fence_event_) {
    CloseHandle(fence_event_);
    fence_event_ = nullptr;
  }
  fence_.Reset();
  active_command_frame_ = nullptr;
  for (CommandFrameContext& command_frame : command_frames_) {
    command_frame.command_list.Reset();
    command_frame.allocator.Reset();
    command_frame.fence_value = 0;
    command_frame.open = false;
  }
  command_queue_.Reset();
  device_.Reset();
#endif
  MaybeLogLiveDiagnostics(submitted_frames_ + failed_frames_, true);
  REXLOG_INFO("BO2 native D3D12 live backend shutdown for {} (submitted={} failed={})",
              app_name_, submitted_frames_, failed_frames_);
}

void D3D12LiveRendererBackend::BeginFrame(uint64_t frame_index) {
  std::lock_guard<std::mutex> builder_lock(frame_builder_mutex_);
  frame_stats_ = pending_stats_;
  pending_stats_ = {};
  in_frame_ = true;
  frame_builder_.BeginFrame(frame_index);
  frame_pending_draws_ = pending_frame_builder_.draw_count();
  frame_builder_.AbsorbPending(pending_frame_builder_, frame_index);
  capture_.WriteBeginFrame(frame_index);
#if defined(_WIN32)
  if (!gpu_faulted_) {
    BeginCommandFrame(frame_index);
  }
#else
  BeginCommandFrame(frame_index);
#endif
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
  if (snapshot.width > 0 && snapshot.height > 0) {
    frame_width_ = snapshot.width;
    frame_height_ = snapshot.height;
  }
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
  std::lock_guard<std::mutex> builder_lock(frame_builder_mutex_);
  ++ActiveStats().packets;
  capture_.WritePM4Packet(packet);
}

void D3D12LiveRendererBackend::SubmitPM4Draw(const PM4DrawInfo &draw) {
  std::lock_guard<std::mutex> builder_lock(frame_builder_mutex_);
  ++ActiveStats().draws;
  capture_.WritePM4Draw(draw);
  ActiveFrameBuilder().AddDraw(draw, draw.event_index);
}

void D3D12LiveRendererBackend::SubmitPM4Shader(const PM4ShaderInfo &shader) {
  std::lock_guard<std::mutex> builder_lock(frame_builder_mutex_);
  ++ActiveStats().shaders;
  capture_.WritePM4Shader(shader);
  ActiveFrameBuilder().BindShader(shader, shader.event_index);
  if (!shader.payload_missing && !shader.payload_truncated &&
      shader.payload_dword_count > 0 && shader.shader_hash != 0) {
    auto &payload = live_shader_payloads_[{shader.shader_type,
                                           shader.shader_hash}];
    if (payload.empty()) {
      payload.assign(shader.payload_dwords.begin(),
                     shader.payload_dwords.begin() +
                         std::min<std::size_t>(shader.payload_dword_count,
                                               shader.payload_dwords.size()));
    }
  }
}

void D3D12LiveRendererBackend::SubmitPM4Constants(
    const PM4ConstantInfo &constants) {
  std::lock_guard<std::mutex> builder_lock(frame_builder_mutex_);
  ++ActiveStats().constants;
  capture_.WritePM4Constants(constants);
  ActiveFrameBuilder().BindConstants(constants, constants.event_index);
}

void D3D12LiveRendererBackend::SubmitPM4Swap(const PM4SwapInfo &swap) {
  std::lock_guard<std::mutex> builder_lock(frame_builder_mutex_);
  ++ActiveStats().swaps;
  capture_.WritePM4Swap(swap);
}

void D3D12LiveRendererBackend::SubmitRenderCommand(
    const RenderCommand &command) {
  capture_.WriteRenderCommand(command);
}

void D3D12LiveRendererBackend::EndFrame(uint64_t frame_index) {
  capture_.WriteEndFrame(frame_index);

  uint64_t frame_draws = 0;
  {
    std::lock_guard<std::mutex> builder_lock(frame_builder_mutex_);
    frame_draws = frame_builder_.draw_count();
  }
  last_submit_stats_ = {};
  bool native_backend_available = true;
#if defined(_WIN32)
  native_backend_available = !gpu_faulted_ && active_command_frame_ != nullptr;
#endif
  bool attempted_submit = frame_draws > 0 && native_backend_available;
  bool submit_success = false;
  if (attempted_submit) {
    last_error_.clear();
    if (!SubmitLiveFrame(frame_index)) {
      ++failed_frames_;
      if (verbose_) {
        REXLOG_WARN("BO2 native D3D12 live frame {} submit failed: {}",
                    frame_index, last_error_);
      }
    } else {
      ++submitted_frames_;
      submit_success = true;
    }
  } else if (frame_draws > 0 && !native_backend_available) {
    ++failed_frames_;
#if defined(_WIN32)
    last_error_ = gpu_faulted_
                      ? "native_d3d12 disabled after GPU fence timeout"
                      : "native_d3d12 has no active command frame";
#else
    last_error_ = "native_d3d12 backend is unavailable";
#endif
    if (verbose_ || failed_frames_ <= 3 || ShouldLogHighFrequencyEvent(frame_index)) {
      REXLOG_WARN("BO2 native D3D12 live frame {} skipped: {}",
                  frame_index, last_error_);
    }
  }
  const bool execute_native_frame =
      submit_success && (last_submit_stats_.submitted_draws > 0 ||
                         last_submit_stats_.copied_retained_frame);
  const bool present_native_frame =
      submit_success && (last_submit_stats_.presentable_frame ||
                         last_submit_stats_.copied_retained_frame);
  capture_.WriteLiveD3D12Submit(frame_index, frame_pending_draws_, frame_draws,
                                attempted_submit, submit_success,
                                submitted_frames_, failed_frames_,
                                last_submit_stats_.submitted_draws,
                                last_submit_stats_.shader_pair_count,
                                last_submit_stats_.pso_entries,
                                last_submit_stats_.pso_cache_misses,
                                last_submit_stats_.pso_cache_hits,
                                last_submit_stats_.diagnostic_pipelines,
                                last_submit_stats_.input_layout_variants,
                                last_submit_stats_.scene_candidate_draws,
                                last_submit_stats_.depth_only_draws,
                                last_submit_stats_.utility_draws,
                                last_submit_stats_.skipped_draws,
                                last_submit_stats_.elided_noop_draws,
                                last_submit_stats_
                                    .candidate_presented_guest_color_base,
                                last_submit_stats_
                                    .selected_presented_guest_color_base,
                                last_submit_stats_.unsupported_reasons,
                                last_submit_stats_.presentable_frame,
                                last_submit_stats_.noop_utility_frame,
                                last_submit_stats_.retained_color_ready,
                                last_submit_stats_.copied_retained_frame,
                                last_submit_stats_
                                    .rejected_presented_guest_color_switch,
                                present_native_frame,
                                last_error_);
  RecordLiveSubmitDiagnostics(frame_index, attempted_submit, submit_success,
                              present_native_frame);

#if defined(_WIN32)
  if (active_command_frame_) {
    EndCommandFrame(frame_index, execute_native_frame);
  }
#else
  EndCommandFrame(frame_index, execute_native_frame);
#endif

#if defined(_WIN32)
  if (!gpu_faulted_ && swapchain_ready_ && swap_chain_) {
    PumpNativeWindowMessages();
    if (present_native_frame) {
      const HRESULT hr = swap_chain_->Present(0, 0);
      if (FAILED(hr)) {
        REXLOG_ERROR("BO2 native D3D12 Present failed frame={} hr={:#010x}",
                     frame_index, static_cast<uint32_t>(hr));
      }
    }
    PumpNativeWindowMessages();
  }
  if (!gpu_faulted_ && device_) {
    const HRESULT removed_reason = device_->GetDeviceRemovedReason();
    if (FAILED(removed_reason)) {
      REXLOG_ERROR(
          "BO2 native D3D12 DEVICE REMOVED frame={} reason={:#010x}",
          frame_index, static_cast<uint32_t>(removed_reason));
      MarkGpuFault(frame_index, "D3D12 device removed");
    }
  }
  if (!gpu_faulted_ && info_queue_) {
    const UINT64 message_count = info_queue_->GetNumStoredMessages();
    std::vector<char> message_buffer;
    for (UINT64 i = 0; i < message_count && i < 32; ++i) {
      SIZE_T length = 0;
      info_queue_->GetMessage(i, nullptr, &length);
      message_buffer.resize(length);
      auto *message =
          reinterpret_cast<D3D12_MESSAGE *>(message_buffer.data());
      if (SUCCEEDED(info_queue_->GetMessage(i, message, &length)) &&
          message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
        REXLOG_WARN("BO2 native D3D12 debug[{}]: {}", int(message->Severity),
                    message->pDescription);
      }
    }
    info_queue_->ClearStoredMessages();
  }
#endif

  if (verbose_ && ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO(
        "BO2 native D3D12 frame {} end packets={} draws={} shaders={} "
        "constants={} swaps={} submitted_frames={} failed_frames={}",
        frame_index, frame_stats_.packets, frame_stats_.draws,
        frame_stats_.shaders, frame_stats_.constants, frame_stats_.swaps,
        submitted_frames_, failed_frames_);
  }
  MaybeLogLiveDiagnostics(frame_index);

  {
    const auto now = std::chrono::steady_clock::now();
    if (last_frame_end_time_.time_since_epoch().count() != 0) {
      const double frame_ms =
          std::chrono::duration<double, std::milli>(now - last_frame_end_time_)
              .count();
      smoothed_frame_ms_ = smoothed_frame_ms_ <= 0.0
                               ? frame_ms
                               : smoothed_frame_ms_ * 0.9 + frame_ms * 0.1;
    }
    last_frame_end_time_ = now;
    NativeRendererLiveStats stats;
    stats.active = true;
    stats.backend = "d3d12-live";
    stats.pipeline = live_pipeline_;
    stats.frame_time_ms = smoothed_frame_ms_;
    stats.fps = smoothed_frame_ms_ > 0.0 ? 1000.0 / smoothed_frame_ms_ : 0.0;
    stats.frames_submitted = submitted_frames_;
    stats.frames_failed = failed_frames_;
    stats.draws_last_frame = last_submit_stats_.submitted_draws;
    stats.skipped_draws_last_frame = last_submit_stats_.skipped_draws;
    stats.shader_pairs = last_submit_stats_.shader_pair_count;
    stats.pso_entries = last_submit_stats_.pso_entries;
    stats.pso_cache_hits = last_submit_stats_.pso_cache_hits;
    stats.pso_cache_misses = last_submit_stats_.pso_cache_misses;
    stats.live_translated_shaders = live_translated_count_;
    stats.presented_guest_color_base =
        last_submit_stats_.selected_presented_guest_color_base;
    PublishNativeRendererLiveStats(stats);
  }
  in_frame_ = false;
}

D3D12LiveRendererBackend::FrameStats &D3D12LiveRendererBackend::ActiveStats() {
  return in_frame_ ? frame_stats_ : pending_stats_;
}

D3D12LiveFrameBuilder &D3D12LiveRendererBackend::ActiveFrameBuilder() {
  return in_frame_ ? frame_builder_ : pending_frame_builder_;
}

#if defined(BO2_HAVE_XENOS_DXBC_TRANSLATOR)
namespace {
bool TranslateLiveXeniaShaderThunk(void *context, uint32_t stage,
                                   uint64_t runtime_hash,
                                   replay::XeniaTranslatedShaderResult &result,
                                   std::string &error) {
  return static_cast<D3D12LiveRendererBackend *>(context)->TranslateLiveShader(
      stage, runtime_hash, result, error);
}
}  // namespace
#endif

bool D3D12LiveRendererBackend::TranslateLiveShader(
    uint32_t stage, uint64_t runtime_hash,
    replay::XeniaTranslatedShaderResult &result, std::string &error) {
#if !defined(BO2_HAVE_XENOS_DXBC_TRANSLATOR)
  (void)stage;
  (void)runtime_hash;
  (void)result;
  error = "live shader translation is not compiled into this target";
  return false;
#else
  std::vector<uint32_t> payload;
  {
    std::lock_guard<std::mutex> builder_lock(frame_builder_mutex_);
    auto it = live_shader_payloads_.find({stage, runtime_hash});
    if (it == live_shader_payloads_.end() || it->second.empty()) {
      error = "no complete live payload for this shader";
      return false;
    }
    payload = it->second;
  }

  XenosDxbcTranslationInput input;
  input.runtime_stage = stage;
  input.runtime_hash = runtime_hash;
  input.payload_dwords = payload.data();
  input.payload_dword_count = payload.size();
  XenosDxbcTranslationResult translated;
  if (!TranslateXenosPayloadToDxbc(input, translated, error)) {
    return false;
  }

  auto hex64 = [](uint64_t value) {
    char buffer[19];
    std::snprintf(buffer, sizeof(buffer), "0x%016llX",
                  static_cast<unsigned long long>(value));
    return std::string(buffer);
  };
  std::ostringstream texture_bindings;
  for (std::size_t i = 0; i < translated.texture_bindings.size(); ++i) {
    const auto &binding = translated.texture_bindings[i];
    if (i != 0) texture_bindings << ",";
    texture_bindings << binding.fetch_constant << ":" << binding.dimension
                     << ":" << (binding.is_signed ? 1 : 0);
  }
  std::ostringstream sampler_bindings;
  for (std::size_t i = 0; i < translated.sampler_bindings.size(); ++i) {
    const auto &binding = translated.sampler_bindings[i];
    if (i != 0) sampler_bindings << ",";
    sampler_bindings << binding.fetch_constant << ":" << binding.mag_filter
                     << ":" << binding.min_filter << ":" << binding.mip_filter
                     << ":" << binding.aniso_filter;
  }
  std::ostringstream float_bitmap;
  float_bitmap << hex64(translated.float_bitmap[0]) << ","
               << hex64(translated.float_bitmap[1]) << ","
               << hex64(translated.float_bitmap[2]) << ","
               << hex64(translated.float_bitmap[3]);

  result.dxbc = translated.dxbc;
  result.modification = translated.modification;
  result.uses_memexport = translated.uses_memexport;
  result.float_bitmap = float_bitmap.str();
  result.texture_bindings = texture_bindings.str();
  result.sampler_bindings = sampler_bindings.str();

  // Persist to the shader cache so later runs load from disk.
  const std::string stage_name = stage == 0 ? "VS" : "PS";
  const std::string stem = stage_name + "_" + hex64(runtime_hash) + ".xenia";
  const std::filesystem::path blob_path =
      shader_cache_root_ / "d3d12" / (stem + ".dxbc");
  std::error_code ec;
  std::filesystem::create_directories(blob_path.parent_path(), ec);
  std::ofstream blob_file(blob_path, std::ios::binary | std::ios::trunc);
  if (blob_file) {
    blob_file.write(reinterpret_cast<const char *>(translated.dxbc.data()),
                    std::streamsize(translated.dxbc.size()));
  }
  std::ostringstream record;
  record << "{\"backend\":\"d3d12\",\"format\":\"xenia_dxbc\","
         << "\"compiler\":\"DxbcShaderTranslator\",\"diagnostic\":false,"
         << "\"translator\":\"" << XenosDxbcTranslatorVersion() << "\","
         << "\"binding_layout\":\"xenia_v1\","
         << "\"stage\":\"" << stage_name << "\","
         << "\"runtime_hash\":\"" << hex64(runtime_hash) << "\","
         << "\"modification\":\"" << hex64(translated.modification)
         << "\","
         << "\"uses_vertex_fetch\":"
         << (translated.uses_vertex_fetch ? "true" : "false") << ","
         << "\"uses_texture_fetch\":"
         << (translated.uses_texture_fetch ? "true" : "false") << ","
         << "\"uses_memexport\":"
         << (translated.uses_memexport ? "true" : "false") << ","
         << "\"float_bitmap\":\"" << float_bitmap.str() << "\","
         << "\"texture_bindings\":\"" << texture_bindings.str() << "\","
         << "\"sampler_bindings\":\"" << sampler_bindings.str() << "\","
         << "\"cache_key\":\"" << stem << "\","
         << "\"path\":\"" << blob_path.generic_string() << "\"}\n";
  std::ofstream index_file(shader_cache_root_ / "shader_cache_index.jsonl",
                           std::ios::binary | std::ios::app);
  if (index_file) {
    index_file << record.str();
  }
  REXLOG_INFO("BO2 native D3D12 live-translated shader {} {} ({} bytes)",
              stage_name, hex64(runtime_hash), translated.dxbc.size());
  ++live_translated_count_;
  return true;
#endif
}

replay::ReplayCliOptions D3D12LiveRendererBackend::BuildReplayOptions() const {
  replay::ReplayCliOptions options{};
  options.shader_cache_root = shader_cache_root_;
  options.shader_override_root = shader_override_root_;
  options.skip_unsupported = skip_unsupported_draws_;
  options.allow_diagnostic_shader = allow_diagnostic_shader_;
  options.frame_index = 0;
  if (live_pipeline_ == "xenia" || live_pipeline_ == "d3d12-xenia") {
    options.backend = "d3d12-xenia";
#if defined(BO2_HAVE_XENOS_DXBC_TRANSLATOR)
    options.translate_xenia_shader = &TranslateLiveXeniaShaderThunk;
    options.translate_xenia_shader_context =
        const_cast<D3D12LiveRendererBackend *>(this);
#endif
  }
  return options;
}

bool D3D12LiveRendererBackend::SubmitLiveFrame(uint64_t frame_index) {
#if !defined(_WIN32)
  (void)frame_index;
  return false;
#else
  if (gpu_faulted_) {
    last_error_ = "native_d3d12 disabled after GPU fence timeout";
    return false;
  }
  const uint32_t width = std::max<uint32_t>(frame_width_, 64u);
  const uint32_t height = std::max<uint32_t>(frame_height_, 64u);
  if (!EnsureSwapChain(width, height)) {
    return false;
  }

  std::string replay_error;
  if (!RefreshSwapChainBackBuffer(replay_error)) {
    last_error_ = replay_error;
    return false;
  }
  PumpNativeWindowMessages();

  if (!active_command_frame_ || !active_command_frame_->command_list) {
    last_error_ = "No active native_d3d12 command frame for live submit";
    return false;
  }

  replay::D3D12LiveSubmitBinding binding{};
  binding.device = device_.Get();
  binding.queue = command_queue_.Get();
  binding.command_list = active_command_frame_->command_list.Get();
  binding.color_target = color_target_.Get();
  binding.rtv_heap = rtv_heap_.Get();
  binding.rtv_descriptor_index = swap_chain_->GetCurrentBackBufferIndex();
  binding.width = width;
  binding.height = height;
  binding.session = replay_session_;

  replay::ReplayCapture capture;
  {
    std::lock_guard<std::mutex> builder_lock(frame_builder_mutex_);
    capture = frame_builder_.BuildCapture(frame_index);
  }
  if (!replay::RunD3D12LiveFrameBackend(capture, BuildReplayOptions(), binding,
                                        replay_error)) {
    last_error_ = replay_error.empty()
                      ? "RunD3D12LiveFrameBackend failed without details"
                      : replay_error;
    return false;
  }
  PumpNativeWindowMessages();
  last_submit_stats_.submitted_draws = binding.submitted_draws;
  last_submit_stats_.shader_pair_count = binding.shader_pair_count;
  last_submit_stats_.pso_entries = binding.pso_entries;
  last_submit_stats_.pso_cache_misses = binding.pso_cache_misses;
  last_submit_stats_.pso_cache_hits = binding.pso_cache_hits;
  last_submit_stats_.diagnostic_pipelines = binding.diagnostic_pipelines;
  last_submit_stats_.input_layout_variants = binding.input_layout_variants;
  last_submit_stats_.scene_candidate_draws = binding.scene_candidate_draws;
  last_submit_stats_.depth_only_draws = binding.depth_only_draws;
  last_submit_stats_.utility_draws = binding.utility_draws;
  last_submit_stats_.skipped_draws = binding.skipped_draws;
  last_submit_stats_.elided_noop_draws = binding.elided_noop_draws;
  last_submit_stats_.candidate_presented_guest_color_base =
      binding.candidate_presented_guest_color_base;
  last_submit_stats_.selected_presented_guest_color_base =
      binding.selected_presented_guest_color_base;
  last_submit_stats_.unsupported_reasons = binding.unsupported_reasons;
  last_submit_stats_.presentable_frame = binding.presentable_frame;
  last_submit_stats_.noop_utility_frame = binding.noop_utility_frame;
  last_submit_stats_.retained_color_ready = binding.retained_color_ready;
  last_submit_stats_.copied_retained_frame = binding.copied_retained_frame;
  last_submit_stats_.rejected_presented_guest_color_switch =
      binding.rejected_presented_guest_color_switch;
  if (verbose_ || submitted_frames_ < 5 || ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO(
        "BO2 native D3D12 live frame {} submitted real_draws={} "
        "shader_pairs={} pso_entries={} pso_misses={} pso_hits={} "
        "diagnostic_pipelines={} "
        "input_layout_variants={} scene_draws={} depth_only_draws={} "
        "utility_draws={} skipped_draws={} elided_noop_draws={} "
        "presentable_frame={} noop_utility_frame={} retained_ready={} "
        "copied_retained_frame={} presented_target_candidate={:#010x} "
        "presented_target_selected={:#010x} rejected_target_switch={}",
        frame_index, binding.submitted_draws, binding.shader_pair_count,
        binding.pso_entries, binding.pso_cache_misses, binding.pso_cache_hits,
        binding.diagnostic_pipelines, binding.input_layout_variants,
        binding.scene_candidate_draws,
        binding.depth_only_draws, binding.utility_draws,
        binding.skipped_draws, binding.elided_noop_draws,
        binding.presentable_frame ? "yes" : "no",
        binding.noop_utility_frame ? "yes" : "no",
        binding.retained_color_ready ? "yes" : "no",
        binding.copied_retained_frame ? "yes" : "no",
        binding.candidate_presented_guest_color_base,
        binding.selected_presented_guest_color_base,
        binding.rejected_presented_guest_color_switch ? "yes" : "no");
  }
  return true;
#endif
}

void D3D12LiveRendererBackend::RecordLiveSubmitDiagnostics(
    uint64_t frame_index, bool attempted_submit, bool submit_success,
    bool present_native_frame) {
  (void)frame_index;
  if (!attempted_submit) {
    return;
  }
  ++live_diagnostics_.frames_attempted;
  if (submit_success) {
    ++live_diagnostics_.frames_submitted;
  }
  if (present_native_frame) {
    ++live_diagnostics_.frames_presentable;
  } else {
    ++live_diagnostics_.frames_not_presentable;
  }
  if (last_submit_stats_.copied_retained_frame) {
    ++live_diagnostics_.frames_retained_copy;
  }
  if (!last_submit_stats_.presentable_frame &&
      !last_submit_stats_.retained_color_ready) {
    ++live_diagnostics_.frames_retained_blocked;
  }
  live_diagnostics_.skipped_draws += last_submit_stats_.skipped_draws;
  live_diagnostics_.elided_noop_draws += last_submit_stats_.elided_noop_draws;
  live_diagnostics_.pso_cache_misses += last_submit_stats_.pso_cache_misses;
  live_diagnostics_.pso_cache_hits += last_submit_stats_.pso_cache_hits;
  live_diagnostics_.diagnostic_pipelines +=
      last_submit_stats_.diagnostic_pipelines;
  for (const auto &[reason, count] : last_submit_stats_.unsupported_reasons) {
    live_diagnostics_.unsupported_reasons[reason] += count;
  }
}

void D3D12LiveRendererBackend::MaybeLogLiveDiagnostics(
    uint64_t frame_index, bool force) const {
  if (!force && frame_index >= 5 && !ShouldLogHighFrequencyEvent(frame_index)) {
    return;
  }
  if (live_diagnostics_.frames_attempted == 0) {
    return;
  }

  std::vector<std::pair<std::string, uint64_t>> reasons(
      live_diagnostics_.unsupported_reasons.begin(),
      live_diagnostics_.unsupported_reasons.end());
  std::sort(reasons.begin(), reasons.end(),
            [](const auto &lhs, const auto &rhs) {
              if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
              }
              return lhs.first < rhs.first;
            });
  std::ostringstream top_reasons;
  const std::size_t top_count = std::min<std::size_t>(3, reasons.size());
  for (std::size_t i = 0; i < top_count; ++i) {
    if (i != 0) {
      top_reasons << " | ";
    }
    top_reasons << reasons[i].second << " x " << reasons[i].first;
  }
  if (top_count == 0) {
    top_reasons << "none";
  }

  REXLOG_INFO(
      "BO2 native D3D12 diagnostics frames_attempted={} submitted={} "
      "presented_or_retained={} not_presentable={} retained_copies={} "
      "retained_blocked={} skipped_draws={} elided_noop_draws={} "
      "pso_misses={} pso_hits={} diagnostic_pipelines={} top_unsupported=[{}]",
      live_diagnostics_.frames_attempted, live_diagnostics_.frames_submitted,
      live_diagnostics_.frames_presentable,
      live_diagnostics_.frames_not_presentable,
      live_diagnostics_.frames_retained_copy,
      live_diagnostics_.frames_retained_blocked,
      live_diagnostics_.skipped_draws, live_diagnostics_.elided_noop_draws,
      live_diagnostics_.pso_cache_misses, live_diagnostics_.pso_cache_hits,
      live_diagnostics_.diagnostic_pipelines, top_reasons.str());
}

bool D3D12LiveRendererBackend::EnsureSwapChain(uint32_t width,
                                               uint32_t height) {
#if !defined(_WIN32)
  (void)width;
  (void)height;
  return false;
#else
  if (!EnsureNativeWindow(width, height)) {
    return false;
  }

  if (swapchain_ready_ && swap_width_ == width && swap_height_ == height) {
    return true;
  }

  if (swap_chain_) {
    if (!WaitForGpu()) {
      MarkGpuFault(0, "timed out before swapchain resize/reset");
      return false;
    }
    swap_chain_.Reset();
    rtv_heap_.Reset();
    color_target_.Reset();
  }

  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = width;
  desc.Height = height;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  desc.Scaling = DXGI_SCALING_STRETCH;

  Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain1;
  HRESULT hr = dxgi_factory_->CreateSwapChainForHwnd(
      command_queue_.Get(), window_handle_, &desc, nullptr, nullptr,
      &swap_chain1);
  if (FAILED(hr)) {
    last_error_ = "CreateSwapChainForHwnd failed for native_d3d12 live backend hr=" +
                  std::to_string(static_cast<uint32_t>(hr));
    REXLOG_ERROR("BO2 native D3D12 swapchain create failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }
  hr = swap_chain1.As(&swap_chain_);
  if (FAILED(hr)) {
    last_error_ = "IDXGISwapChain1::As(IDXGISwapChain3) failed";
    return false;
  }

  dxgi_factory_->MakeWindowAssociation(window_handle_,
                                       DXGI_MWA_NO_ALT_ENTER);

  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = desc.BufferCount;
  hr = device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_));
  if (FAILED(hr)) {
    last_error_ = "CreateDescriptorHeap(RTV) failed for native_d3d12 swapchain";
    return false;
  }

  swap_width_ = width;
  swap_height_ = height;
  swapchain_ready_ = true;
  return true;
#endif
}

bool D3D12LiveRendererBackend::EnsureNativeWindow(uint32_t width,
                                                  uint32_t height) {
#if !defined(_WIN32)
  (void)width;
  (void)height;
  return false;
#else
  if (window_handle_) {
    return true;
  }

  {
    std::scoped_lock lock(window_mutex_);
    window_ready_ = false;
    window_failed_ = false;
  }
  window_thread_stop_.store(false);
  window_thread_ =
      std::thread(&D3D12LiveRendererBackend::NativeWindowThreadMain, this,
                  width, height);

  std::unique_lock lock(window_mutex_);
  if (!window_cv_.wait_for(lock, std::chrono::seconds(5), [this] {
        return window_ready_ || window_failed_;
      })) {
    last_error_ = "Timed out creating native_d3d12 render window";
    window_thread_stop_.store(true);
    return false;
  }
  if (window_failed_ || !window_handle_) {
    last_error_ = "CreateWindowExW failed for native_d3d12 render window";
    window_thread_stop_.store(true);
    return false;
  }
  return true;
#endif
}

void D3D12LiveRendererBackend::NativeWindowThreadMain(uint32_t width,
                                                      uint32_t height) {
#if defined(_WIN32)
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  constexpr const wchar_t *kClassName = L"BO2NativeD3D12RenderWindow";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = NativeD3D12WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);

  RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  std::wstring title = L"BO2 Native D3D12";
  if (!app_name_.empty()) {
    title += L" - ";
    title += WidenAscii(app_name_);
  }
  // WS_EX_NOACTIVATE keeps this preview window from stealing foreground focus
  // from the ReXGlue game window, which owns keyboard/mouse (mnk_mode) input;
  // without it the game never receives menu navigation while the native window
  // is up.
  HWND hwnd = CreateWindowExW(
      WS_EX_NOACTIVATE, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
      rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
  if (!hwnd) {
    {
      std::scoped_lock lock(window_mutex_);
      window_failed_ = true;
    }
    window_cv_.notify_all();
    return;
  }
  {
    std::scoped_lock lock(window_mutex_);
    window_handle_ = hwnd;
    owns_window_ = true;
    window_ready_ = true;
  }
  window_cv_.notify_all();
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd);

  MSG msg{};
  while (!window_thread_stop_.load()) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        window_thread_stop_.store(true);
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!window_thread_stop_.load()) {
      MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
    }
  }
  HWND cleanup_hwnd = nullptr;
  {
    std::scoped_lock lock(window_mutex_);
    cleanup_hwnd = window_handle_;
    window_handle_ = nullptr;
    owns_window_ = false;
    window_ready_ = false;
  }
  if (cleanup_hwnd && IsWindow(cleanup_hwnd)) {
    DestroyWindow(cleanup_hwnd);
  }
#endif
}

bool D3D12LiveRendererBackend::RefreshSwapChainBackBuffer(std::string &error) {
#if !defined(_WIN32)
  (void)error;
  return false;
#else
  if (!swap_chain_ || !rtv_heap_) {
    error = "swapchain not ready for native_d3d12 back buffer refresh";
    return false;
  }

  const UINT back_buffer_index = swap_chain_->GetCurrentBackBufferIndex();
  color_target_.Reset();
  HRESULT hr =
      swap_chain_->GetBuffer(back_buffer_index, IID_PPV_ARGS(&color_target_));
  if (FAILED(hr)) {
    error = "IDXGISwapChain3::GetBuffer failed for native_d3d12 live backend";
    return false;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      rtv_heap_->GetCPUDescriptorHandleForHeapStart();
  const UINT rtv_descriptor_size =
      device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  rtv.ptr += static_cast<SIZE_T>(back_buffer_index) * rtv_descriptor_size;
  device_->CreateRenderTargetView(color_target_.Get(), nullptr, rtv);
  return true;
#endif
}

void D3D12LiveRendererBackend::PumpNativeWindowMessages() {
#if defined(_WIN32)
  if (!owns_window_ || !window_handle_) {
    return;
  }
  MSG msg{};
  while (PeekMessageW(&msg, window_handle_, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
#endif
}

bool D3D12LiveRendererBackend::BeginCommandFrame(uint64_t frame_index) {
#if !defined(_WIN32)
  (void)frame_index;
  return false;
#else
  if (!command_queue_ || !fence_ || !fence_event_) {
    return false;
  }
  if (active_command_frame_) {
    last_error_ = "native_d3d12 command frame already open";
    REXLOG_ERROR("BO2 native D3D12 command frame overlap at frame={}",
                 frame_index);
    return false;
  }

  CommandFrameContext& command_frame = command_frames_[next_command_frame_];
  if (!command_frame.allocator || !command_frame.command_list) {
    return false;
  }
  if (!WaitForFenceValue(command_frame.fence_value)) {
    MarkGpuFault(frame_index, "timed out waiting for reusable command frame");
    return false;
  }

  HRESULT hr = command_frame.allocator->Reset();
  if (FAILED(hr)) {
    last_error_ = "Reset command allocator failed for native_d3d12 live frame";
    REXLOG_ERROR("BO2 native D3D12 command allocator reset failed frame={} "
                 "hr={:#010x}",
                 frame_index, static_cast<uint32_t>(hr));
    return false;
  }
  hr = command_frame.command_list->Reset(command_frame.allocator.Get(),
                                         nullptr);
  if (FAILED(hr)) {
    last_error_ = "Reset command list failed for native_d3d12 live frame";
    REXLOG_ERROR("BO2 native D3D12 command list reset failed frame={} "
                 "hr={:#010x}",
                 frame_index, static_cast<uint32_t>(hr));
    return false;
  }

  command_frame.open = true;
  active_command_frame_ = &command_frame;
  next_command_frame_ = (next_command_frame_ + 1) % kLiveCommandFrameCount;
  return true;
#endif
}

void D3D12LiveRendererBackend::EndCommandFrame(uint64_t frame_index,
                                               bool execute) {
#if !defined(_WIN32)
  (void)frame_index;
  (void)execute;
#else
  if (!active_command_frame_ || !active_command_frame_->open ||
      !active_command_frame_->command_list || !command_queue_) {
    return;
  }
  CommandFrameContext* command_frame = active_command_frame_;
  active_command_frame_ = nullptr;
  command_frame->open = false;

  HRESULT hr = command_frame->command_list->Close();
  if (FAILED(hr)) {
    last_error_ = "Close command list failed for native_d3d12 live frame";
    REXLOG_ERROR("BO2 native D3D12 command list close failed frame={} "
                 "hr={:#010x}",
                 frame_index, static_cast<uint32_t>(hr));
    return;
  }

  if (!execute) {
    return;
  }

  ID3D12CommandList *lists[] = {command_frame->command_list.Get()};
  command_queue_->ExecuteCommandLists(1, lists);

  const uint64_t signal_value = ++fence_value_;
  hr = command_queue_->Signal(fence_.Get(), signal_value);
  if (FAILED(hr)) {
    last_error_ = "Signal frame fence failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 frame fence signal failed frame={} "
                 "hr={:#010x}",
                 frame_index, static_cast<uint32_t>(hr));
    return;
  }
  command_frame->fence_value = signal_value;
#endif
}

void D3D12LiveRendererBackend::MarkGpuFault(uint64_t frame_index,
                                            std::string_view reason) {
#if defined(_WIN32)
  if (gpu_faulted_) {
    return;
  }
  gpu_faulted_ = true;
  gpu_fault_frame_ = frame_index;
  active_command_frame_ = nullptr;
  for (CommandFrameContext& command_frame : command_frames_) {
    command_frame.open = false;
  }
  last_error_ = "native_d3d12 disabled after GPU fault: ";
  last_error_ += std::string(reason);
  REXLOG_ERROR(
      "BO2 native D3D12 disabled at frame {} after GPU fault: {}. "
      "Native submits/presents are stopped to avoid desktop stalls.",
      frame_index, std::string(reason));
#else
  (void)frame_index;
  (void)reason;
#endif
}

bool D3D12LiveRendererBackend::WaitForFenceValue(uint64_t signal_value) {
#if !defined(_WIN32)
  (void)signal_value;
  return false;
#else
  if (gpu_faulted_) {
    return false;
  }
  if (signal_value == 0) {
    return true;
  }
  if (!fence_ || !fence_event_) {
    return false;
  }

  if (fence_->GetCompletedValue() >= signal_value) {
    return true;
  }

  HRESULT hr = fence_->SetEventOnCompletion(signal_value, fence_event_);
  if (FAILED(hr)) {
    last_error_ = "SetEventOnCompletion failed for native_d3d12 live backend";
    REXLOG_ERROR("BO2 native D3D12 fence wait setup failed hr={:#010x}",
                 static_cast<uint32_t>(hr));
    return false;
  }
  constexpr DWORD kWaitSliceMs = 16;
  constexpr DWORD kWaitTimeoutMs = 2000;
  DWORD waited_ms = 0;
  while (fence_->GetCompletedValue() < signal_value) {
    const DWORD result =
        MsgWaitForMultipleObjects(1, &fence_event_, FALSE, kWaitSliceMs,
                                  QS_ALLINPUT);
    if (result == WAIT_OBJECT_0) {
      return true;
    }
    if (result == WAIT_OBJECT_0 + 1) {
      PumpNativeWindowMessages();
      continue;
    }
    if (result == WAIT_TIMEOUT) {
      waited_ms += kWaitSliceMs;
      if (waited_ms >= kWaitTimeoutMs) {
        last_error_ = "Timed out waiting for native_d3d12 GPU fence";
        REXLOG_ERROR("BO2 native D3D12 fence wait timed out after {} ms",
                     waited_ms);
        return false;
      }
      continue;
    }
    last_error_ = "native_d3d12 GPU fence wait failed";
    REXLOG_ERROR("BO2 native D3D12 fence wait failed result={:#010x}",
                 result);
    return false;
  }
  return true;
#endif
}

bool D3D12LiveRendererBackend::WaitForGpu() {
#if !defined(_WIN32)
  return false;
#else
  if (gpu_faulted_) {
    return false;
  }
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
  return WaitForFenceValue(signal_value);
#endif
}

}  // namespace bo2::native
