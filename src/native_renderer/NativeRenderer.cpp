#include "NativeRenderer.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <excpt.h>
#endif

#include <rex/cvar.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/function.h>
#include <rex/system/kernel_state.h>

#include "DebugRenderLog.h"
#include "RendererBackend.h"
#include "backend_d3d12/D3D12LiveRendererBackend.h"
#include "backend_null/NullRendererBackend.h"

REXCVAR_DEFINE_STRING(native_renderer_mode, "emulated", "Renderer",
                      "Renderer mode: emulated, native, native_null, native_d3d12");
REXCVAR_DEFINE_BOOL(native_renderer_verbose, true, "Renderer",
                    "Enable verbose native renderer logging");
REXCVAR_DEFINE_STRING(native_renderer_shader_cache_root, "shader_work/cache",
                      "Renderer",
                      "Root directory for native D3D12 shader cache index/HLSL");
REXCVAR_DEFINE_STRING(native_renderer_shader_override_root,
                      "shader_work/native_overrides", "Renderer",
                      "Root directory for native shader override manifests");
REXCVAR_DEFINE_BOOL(native_renderer_skip_unsupported_draws, true, "Renderer",
                    "Skip unsupported PM4 draws in native_d3d12 live mode");
REXCVAR_DEFINE_BOOL(
    native_renderer_live_allow_diagnostic_shader, false, "Renderer",
    "Allow native_d3d12 live mode to present diagnostic shader fallback output");
REXCVAR_DEFINE_STRING(native_renderer_shader_record_probe_mode, "off", "Renderer",
                      "Shader/material record probe capture: off, on");
REXCVAR_DEFINE_UINT32(native_renderer_shader_record_probe_dwords, 32, "Renderer",
                      "Maximum dwords to snapshot from shader/material record probe pointers")
    .range(1, bo2::native::ShaderRecordProbeInfo::kMaxRecordDwords);

namespace bo2::native {

namespace {

std::string Normalize(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  std::ranges::replace(value, '-', '_');
  return value;
}

RendererBackendKind BackendForMode(RendererMode mode) {
  switch (mode) {
    case RendererMode::Emulated:
      return RendererBackendKind::None;
    case RendererMode::Native:
    case RendererMode::NativeNull:
      return RendererBackendKind::NullDebug;
    case RendererMode::NativeD3D12:
      return RendererBackendKind::D3D12Live;
  }
  return RendererBackendKind::None;
}

std::filesystem::path ResolveProjectRelativePath(std::filesystem::path path) {
  if (path.empty() || path.is_absolute()) {
    return path;
  }

  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) {
    const std::filesystem::path cwd_relative = cwd / path;
    if (std::filesystem::exists(cwd_relative, ec) && !ec) {
      return std::filesystem::absolute(cwd_relative, ec);
    }

    for (std::filesystem::path probe = cwd; !probe.empty();
         probe = probe.parent_path()) {
      const std::filesystem::path candidate = probe / path;
      if (std::filesystem::exists(candidate, ec) && !ec) {
        return std::filesystem::absolute(candidate, ec);
      }
      if (probe == probe.root_path() || probe == probe.parent_path()) {
        break;
      }
    }
  }

  return path;
}

uint32_t ReadGuestArg(PPCContext& ctx, uint8_t* base, std::size_t arg) {
  return static_cast<uint32_t>(
      rex::ppc::ArgTranslator::GetIntegerArgumentValue(ctx, base, arg));
}

bool ReadGuestU32Checked(uint32_t address, uint32_t& value) {
  if (!address) {
    value = 0;
    return false;
  }
  const auto* ptr = REX_KERNEL_MEMORY()->TranslateVirtual<const uint32_t*>(address);
  if (!ptr) {
    value = 0;
    return false;
  }
#if defined(_WIN32)
  __try {
    value = rex::memory::load_and_swap<uint32_t>(ptr);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    value = 0;
    return false;
  }
#else
  value = rex::memory::load_and_swap<uint32_t>(ptr);
  return true;
#endif
}

uint32_t ReadGuestU32(uint32_t address) {
  uint32_t value = 0;
  ReadGuestU32Checked(address, value);
  return value;
}

bool IsType3Packet(uint32_t packet, rex::graphics::xenos::Type3Opcode opcode) {
  return ((packet >> 30) == 3) &&
         (((packet >> 8) & 0x7F) == static_cast<uint32_t>(opcode));
}

bool LooksLikeGuestPointer(uint32_t address) {
  if ((address & 3u) != 0 || address == UINT32_MAX) {
    return false;
  }
  // BO2 commonly passes shader/material records through the 0xA/0xB physical
  // aliases used by the Xbox memory map, while code/data records live in the
  // lower virtual and XEX ranges.
  return address >= 0x00010000u && address < 0xF0000000u;
}

bool IsShaderRecordProbeAddress(uint32_t address) {
  switch (address) {
    // Single-player packet emitters.
    case 0x8258CCE8:
    case 0x8258CE40:
    case 0x82597DF8:
    case 0x82597F50:
    case 0x82598140:
    // Multiplayer packet emitters mapped from the matching XEX build.
    case 0x82117BC8:
    case 0x82117D20:
    case 0x8212D478:
    case 0x8212E280:
    case 0x8212EB40:
      return true;
    default:
      return false;
  }
}

bool ShaderRecordProbesEnabled() {
  return Normalize(REXCVAR_GET(native_renderer_shader_record_probe_mode)) == "on";
}

uint32_t ShaderRecordProbeDwordLimit() {
  return std::clamp<uint32_t>(
      REXCVAR_GET(native_renderer_shader_record_probe_dwords), 1,
      static_cast<uint32_t>(ShaderRecordProbeInfo::kMaxRecordDwords));
}

void CaptureDwordSnapshot(
    uint32_t address, uint32_t dword_count_hint,
    std::array<uint32_t, ShaderRecordProbeInfo::kMaxRecordDwords>& dwords,
    uint32_t& dword_count, bool& truncated, bool& missing) {
  dword_count = 0;
  truncated = false;
  missing = true;
  dwords = {};
  if (!LooksLikeGuestPointer(address)) {
    return;
  }

  const uint32_t requested =
      dword_count_hint == 0 ? ShaderRecordProbeInfo::kMaxRecordDwords
                            : dword_count_hint;
  const uint32_t configured_limit = ShaderRecordProbeDwordLimit();
  const uint32_t limit = std::min<uint32_t>(
      {requested, configured_limit,
       static_cast<uint32_t>(ShaderRecordProbeInfo::kMaxRecordDwords)});
  truncated = requested > limit;

  for (uint32_t i = 0; i < limit; ++i) {
    uint32_t value = 0;
    if (!ReadGuestU32Checked(address + i * 4, value)) {
      truncated = i != 0;
      missing = i == 0;
      return;
    }
    dwords[i] = value;
    ++dword_count;
  }
  missing = dword_count == 0;
}

uint32_t FindNestedGuestPointer(
    const std::array<uint32_t, ShaderRecordProbeInfo::kMaxRecordDwords>& dwords,
    uint32_t dword_count) {
  constexpr std::array<uint32_t, 8> kPreferredOffsets = {12, 16, 8, 10,
                                                         14, 18, 20, 24};
  for (const uint32_t offset : kPreferredOffsets) {
    if (offset < dword_count && LooksLikeGuestPointer(dwords[offset])) {
      return dwords[offset];
    }
  }
  for (uint32_t i = 0; i < dword_count; ++i) {
    if (LooksLikeGuestPointer(dwords[i])) {
      return dwords[i];
    }
  }
  return 0;
}

bool LooksLikeRuntimeHeapPointer(uint32_t value) {
  return LooksLikeGuestPointer(value) && value >= 0x84480000u &&
         value < 0x90000000u;
}

std::size_t AppendRuntimeHeapPointers(
    const std::array<uint32_t, ShaderRecordProbeInfo::kMaxRecordDwords>& dwords,
    uint32_t dword_count, std::array<uint32_t, 64>& candidates,
    std::size_t candidate_count) {
  constexpr std::array<uint32_t, 16> kPreferredOffsets = {
      21, 13, 1, 7, 9, 11, 19, 23, 25, 27, 29, 33, 35, 37, 45, 47};
  auto append = [&](uint32_t value) {
    if (!LooksLikeRuntimeHeapPointer(value) ||
        candidate_count >= candidates.size()) {
      return;
    }
    for (std::size_t i = 0; i < candidate_count; ++i) {
      if (candidates[i] == value) {
        return;
      }
    }
    candidates[candidate_count++] = value;
  };
  for (const uint32_t offset : kPreferredOffsets) {
    if (offset < dword_count) {
      append(dwords[offset]);
    }
  }
  for (uint32_t i = 0; i < dword_count; ++i) {
    append(dwords[i]);
  }
  return candidate_count;
}

bool IsUsefulHeapCandidate(
    const std::array<uint32_t, ShaderRecordProbeInfo::kMaxRecordDwords>& dwords,
    uint32_t dword_count) {
  uint32_t useful = 0;
  for (uint32_t i = 0; i < dword_count; ++i) {
    const uint32_t value = dwords[i];
    if (value != 0 && value != 0xffffffffu && value != 0xbebebebeu) {
      ++useful;
    }
  }
  return useful >= 4;
}

void CaptureHeapCandidate(ShaderRecordProbeInfo& probe) {
  std::array<uint32_t, 64> candidates{};
  std::size_t candidate_count = 0;
  candidate_count = AppendRuntimeHeapPointers(
      probe.tertiary_dwords, probe.tertiary_dword_count, candidates,
      candidate_count);
  candidate_count = AppendRuntimeHeapPointers(
      probe.secondary_dwords, probe.secondary_dword_count, candidates,
      candidate_count);
  candidate_count = AppendRuntimeHeapPointers(
      probe.primary_dwords, probe.primary_dword_count, candidates,
      candidate_count);

  for (std::size_t i = 0; i < candidate_count; ++i) {
    std::array<uint32_t, ShaderRecordProbeInfo::kMaxRecordDwords> dwords{};
    uint32_t dword_count = 0;
    bool truncated = false;
    bool missing = true;
    CaptureDwordSnapshot(candidates[i],
                         ShaderRecordProbeInfo::kMaxRecordDwords,
                         dwords, dword_count, truncated, missing);
    if (!missing && IsUsefulHeapCandidate(dwords, dword_count)) {
      probe.heap_candidate_address = candidates[i];
      probe.heap_candidate_dwords = dwords;
      probe.heap_candidate_dword_count = dword_count;
      probe.heap_candidate_truncated = truncated;
      probe.heap_candidate_missing = false;
      return;
    }
    if (probe.heap_candidate_address == 0 && !missing) {
      probe.heap_candidate_address = candidates[i];
      probe.heap_candidate_dwords = dwords;
      probe.heap_candidate_dword_count = dword_count;
      probe.heap_candidate_truncated = truncated;
      probe.heap_candidate_missing = false;
    }
  }
}

ShaderRecordProbeInfo BuildShaderRecordProbe(
    const CommandBufferEventInfo& event) {
  ShaderRecordProbeInfo probe{};
  probe.event_index = event.event_index;
  probe.function_address = event.function_address;
  probe.function_name = event.function_name;
  probe.link_register = event.link_register;
  probe.r3 = event.r3;
  probe.r4 = event.r4;
  probe.r5 = event.r5;
  probe.r6 = event.r6;
  probe.r7 = event.r7;
  probe.r8 = event.r8;
  probe.r9 = event.r9;
  probe.r10 = event.r10;
  probe.r28 = event.r28;
  probe.r29 = event.r29;
  probe.r30 = event.r30;
  probe.r31 = event.r31;
  probe.command_buffer_object = event.command_buffer_object;
  probe.write_begin = event.write_begin;
  probe.write_end = event.write_end;
  probe.write_limit_begin = event.write_limit_begin;
  probe.write_limit_end = event.write_limit_end;
  probe.return_value = event.return_value;

  probe.primary_address = event.r4;
  probe.primary_dword_count_hint =
      LooksLikeGuestPointer(event.r5)
          ? static_cast<uint32_t>(ShaderRecordProbeInfo::kMaxRecordDwords)
          : event.r5;
  CaptureDwordSnapshot(probe.primary_address, probe.primary_dword_count_hint,
                       probe.primary_dwords, probe.primary_dword_count,
                       probe.primary_truncated, probe.primary_missing);

  if (LooksLikeGuestPointer(event.r5)) {
    probe.secondary_address = event.r5;
    CaptureDwordSnapshot(probe.secondary_address,
                         ShaderRecordProbeInfo::kMaxRecordDwords,
                         probe.secondary_dwords, probe.secondary_dword_count,
                         probe.secondary_truncated, probe.secondary_missing);
  }
  probe.tertiary_address =
      FindNestedGuestPointer(probe.secondary_dwords, probe.secondary_dword_count);
  if (probe.tertiary_address != 0 &&
      probe.tertiary_address != probe.primary_address &&
      probe.tertiary_address != probe.secondary_address) {
    CaptureDwordSnapshot(probe.tertiary_address,
                         ShaderRecordProbeInfo::kMaxRecordDwords,
                         probe.tertiary_dwords, probe.tertiary_dword_count,
                         probe.tertiary_truncated, probe.tertiary_missing);
  }
  probe.quaternary_address =
      FindNestedGuestPointer(probe.tertiary_dwords, probe.tertiary_dword_count);
  if (probe.quaternary_address != 0 &&
      probe.quaternary_address != probe.primary_address &&
      probe.quaternary_address != probe.secondary_address &&
      probe.quaternary_address != probe.tertiary_address) {
    CaptureDwordSnapshot(probe.quaternary_address,
                         ShaderRecordProbeInfo::kMaxRecordDwords,
                         probe.quaternary_dwords,
                         probe.quaternary_dword_count,
                         probe.quaternary_truncated,
                         probe.quaternary_missing);
  }
  CaptureHeapCandidate(probe);
  return probe;
}

ShaderRecordProbeInfo BuildShaderRecordProbe(
    const DrawPacketCandidateInfo& draw) {
  ShaderRecordProbeInfo probe{};
  probe.event_index = draw.event_index;
  probe.function_address = draw.function_address;
  probe.function_name = draw.function_name;
  probe.link_register = draw.link_register;
  probe.r3 = draw.r3;
  probe.r4 = draw.r4;
  probe.r5 = draw.r5;
  probe.r6 = draw.r6;
  probe.r7 = draw.r7;
  probe.r8 = draw.r8;
  probe.r31 = draw.r31;
  probe.command_buffer_object = draw.command_buffer_object;
  probe.write_begin = draw.write_begin;
  probe.write_end = draw.write_end;
  probe.write_limit_begin = draw.write_limit;
  probe.write_limit_end = draw.write_limit;

  probe.primary_address = draw.r5;
  probe.primary_dword_count_hint =
      LooksLikeGuestPointer(draw.r6)
          ? static_cast<uint32_t>(ShaderRecordProbeInfo::kMaxRecordDwords)
          : draw.r6;
  CaptureDwordSnapshot(probe.primary_address, probe.primary_dword_count_hint,
                       probe.primary_dwords, probe.primary_dword_count,
                       probe.primary_truncated, probe.primary_missing);

  if (probe.primary_dword_count > 16 &&
      LooksLikeGuestPointer(probe.primary_dwords[16])) {
    probe.secondary_address = probe.primary_dwords[16];
    CaptureDwordSnapshot(probe.secondary_address,
                         ShaderRecordProbeInfo::kMaxRecordDwords,
                         probe.secondary_dwords, probe.secondary_dword_count,
                         probe.secondary_truncated, probe.secondary_missing);
  } else if (LooksLikeGuestPointer(draw.r7) && draw.r7 != draw.r5) {
    probe.secondary_address = draw.r7;
    CaptureDwordSnapshot(probe.secondary_address,
                         ShaderRecordProbeInfo::kMaxRecordDwords,
                         probe.secondary_dwords, probe.secondary_dword_count,
                         probe.secondary_truncated, probe.secondary_missing);
  }
  probe.tertiary_address =
      FindNestedGuestPointer(probe.secondary_dwords, probe.secondary_dword_count);
  if (probe.tertiary_address != 0 &&
      probe.tertiary_address != probe.primary_address &&
      probe.tertiary_address != probe.secondary_address) {
    CaptureDwordSnapshot(probe.tertiary_address,
                         ShaderRecordProbeInfo::kMaxRecordDwords,
                         probe.tertiary_dwords, probe.tertiary_dword_count,
                         probe.tertiary_truncated, probe.tertiary_missing);
  }
  probe.quaternary_address =
      FindNestedGuestPointer(probe.tertiary_dwords, probe.tertiary_dword_count);
  if (probe.quaternary_address != 0 &&
      probe.quaternary_address != probe.primary_address &&
      probe.quaternary_address != probe.secondary_address &&
      probe.quaternary_address != probe.tertiary_address) {
    CaptureDwordSnapshot(probe.quaternary_address,
                         ShaderRecordProbeInfo::kMaxRecordDwords,
                         probe.quaternary_dwords,
                         probe.quaternary_dword_count,
                         probe.quaternary_truncated,
                         probe.quaternary_missing);
  }
  CaptureHeapCandidate(probe);
  return probe;
}

}  // namespace

const char* ToString(RendererMode mode) {
  switch (mode) {
    case RendererMode::Emulated:
      return "emulated";
    case RendererMode::Native:
      return "native";
    case RendererMode::NativeNull:
      return "native_null";
    case RendererMode::NativeD3D12:
      return "native_d3d12";
  }
  return "unknown";
}

const char* ToString(RendererBackendKind backend) {
  switch (backend) {
    case RendererBackendKind::None:
      return "none";
    case RendererBackendKind::NullDebug:
      return "null_debug";
    case RendererBackendKind::D3D12Live:
      return "d3d12_live";
  }
  return "unknown";
}

RendererMode ParseRendererMode(std::string mode) {
  mode = Normalize(std::move(mode));
  if (mode == "native") {
    return RendererMode::Native;
  }
  if (mode == "native_d3d12" || mode == "d3d12") {
    return RendererMode::NativeD3D12;
  }
  if (mode == "native_null" || mode == "null" || mode == "debug_null") {
    return RendererMode::NativeNull;
  }
  if (mode != "emulated") {
    REXLOG_WARN("Unknown native_renderer_mode '{}'; using emulated renderer", mode);
  }
  return RendererMode::Emulated;
}

NativeRenderer& NativeRenderer::Instance() {
  static NativeRenderer renderer;
  return renderer;
}

void NativeRenderer::ConfigureForApp(std::string_view app_name) {
  config_.app_name = std::string(app_name);
  config_.mode = ParseRendererMode(REXCVAR_GET(native_renderer_mode));
  config_.backend = BackendForMode(config_.mode);
  config_.verbose = REXCVAR_GET(native_renderer_verbose);
  config_.shader_cache_root = ResolveProjectRelativePath(
      std::filesystem::path(REXCVAR_GET(native_renderer_shader_cache_root)));
  config_.shader_override_root = ResolveProjectRelativePath(
      std::filesystem::path(REXCVAR_GET(native_renderer_shader_override_root)));
  config_.skip_unsupported_draws =
      REXCVAR_GET(native_renderer_skip_unsupported_draws);
  config_.allow_live_diagnostic_shader =
      REXCVAR_GET(native_renderer_live_allow_diagnostic_shader);
  configured_ = true;

  REXLOG_INFO("BO2 native renderer mode={} backend={} verbose={} app={}",
              ToString(config_.mode), ToString(config_.backend), config_.verbose,
              config_.app_name);
  if (config_.mode != RendererMode::Emulated) {
    REXLOG_INFO("BO2 native renderer paths shader_cache={} shader_overrides={}",
                config_.shader_cache_root.string(),
                config_.shader_override_root.string());
  }

  if (ShouldInstallHooks()) {
    EnsureBackend();
  }
}

bool NativeRenderer::ShouldInstallHooks() const {
  return config_.mode != RendererMode::Emulated;
}

bool NativeRenderer::ShouldSuppressEmulatedPresent() const {
  return config_.mode == RendererMode::NativeNull ||
         config_.mode == RendererMode::NativeD3D12;
}

void NativeRenderer::OnSystemCommandBufferGpuIdentifierAddress(uint32_t address) {
  system_command_buffer_gpu_identifier_ = address;
  if (config_.verbose) {
    REXLOG_INFO("BO2 native renderer VdSetSystemCommandBufferGpuIdentifierAddress {:#010x}",
                address);
  }
}

VdSwapInfo NativeRenderer::OnVdSwapBegin(PPCContext& ctx, uint8_t* base) {
  VdSwapInfo swap = CaptureVdSwap(ctx, base);
  if (!EnsureBackend()) {
    return swap;
  }

  const uint64_t frame_index = ++vd_swap_count_;
  swap.frame_index = frame_index;
  backend_->BeginFrame(frame_index);
  backend_->SubmitVdSwap(frame_index, swap);
  return swap;
}

bool NativeRenderer::CanForwardVdSwap(const VdSwapInfo& swap) const {
  uint32_t ignored = 0;
  if (!swap.command_buffer ||
      !ReadGuestU32Checked(swap.command_buffer, ignored)) {
    REXLOG_WARN("BO2 native renderer suppressing VdSwap: bad command buffer {:#010x}",
                swap.command_buffer);
    return false;
  }
  for (uint32_t offset = 0; offset < 24; offset += 4) {
    if (!ReadGuestU32Checked(swap.fetch_constant + offset, ignored)) {
      REXLOG_WARN(
          "BO2 native renderer suppressing VdSwap: bad fetch constant {:#010x} "
          "offset={}",
          swap.fetch_constant, offset);
      return false;
    }
  }
  const uint32_t scalar_ptrs[] = {
      swap.frontbuffer_ptr, swap.texture_format_ptr, swap.color_space_ptr,
      swap.width_ptr, swap.height_ptr,
  };
  for (uint32_t address : scalar_ptrs) {
    if (!ReadGuestU32Checked(address, ignored)) {
      REXLOG_WARN("BO2 native renderer suppressing VdSwap: bad scalar pointer {:#010x}",
                  address);
      return false;
    }
  }
  return true;
}

void NativeRenderer::OnVdSwapEnd(const VdSwapInfo& swap, bool command_buffer_written) {
  if (!swap.frame_index || !EnsureBackend()) {
    return;
  }

  if (command_buffer_written) {
    backend_->SubmitCommandBufferSnapshot(
        swap.frame_index, CaptureCommandBufferSnapshot(swap));
  }
  backend_->EndFrame(swap.frame_index);

  if (!command_buffer_written && ShouldLogHighFrequencyEvent(swap.frame_index)) {
    REXLOG_WARN("BO2 native renderer native_null suppressed emulated VdSwap frame={}",
                swap.frame_index);
  }
}

DrawPacketCandidateInfo NativeRenderer::OnDrawPacketCandidateBegin(
    std::string_view function_name, uint32_t function_address, PPCContext& ctx) {
  DrawPacketCandidateInfo draw{};
  if (!EnsureBackend()) {
    return draw;
  }

  draw.event_index = ++draw_candidate_count_;
  draw.function_address = function_address;
  draw.function_name = function_name;
  draw.link_register = ctx.lr;
  draw.r3 = ctx.r3.u32;
  draw.r4 = ctx.r4.u32;
  draw.r5 = ctx.r5.u32;
  draw.r6 = ctx.r6.u32;
  draw.r7 = ctx.r7.u32;
  draw.r8 = ctx.r8.u32;
  draw.r31 = ctx.r31.u32;
  draw.command_buffer_object = ctx.r3.u32;
  if (draw.command_buffer_object) {
    draw.write_begin = ReadGuestU32(draw.command_buffer_object + 48);
    draw.write_limit = ReadGuestU32(draw.command_buffer_object + 56);
  }
  return draw;
}

void NativeRenderer::OnDrawPacketCandidateEnd(DrawPacketCandidateInfo& draw) {
  if (!draw.event_index || !EnsureBackend()) {
    return;
  }
  CaptureDrawPacketWrites(draw);
  backend_->SubmitDrawPacketCandidate(draw);
  if (ShaderRecordProbesEnabled() && IsShaderRecordProbeAddress(draw.function_address)) {
    backend_->SubmitShaderRecordProbe(BuildShaderRecordProbe(draw));
  }

  if (draw.has_draw_indx_2) {
    RenderCommand command{};
    command.type = RenderCommandType::DrawIndexed;
    command.sequence = draw.event_index;
    command.guest_address = draw.write_begin + 4 + draw.draw_packet_dword_offset * 4;
    command.arg0 = draw.index_count;
    command.arg1 = draw.primitive_type;
    command.arg2 = draw.source_select;
    command.arg3 = draw.index_32bit ? 1 : 0;
    backend_->SubmitRenderCommand(command);
  }
}

CommandBufferEventInfo NativeRenderer::OnCommandBufferEventBegin(
    std::string_view function_name, uint32_t function_address, PPCContext& ctx) {
  CommandBufferEventInfo event{};
  if (config_.mode == RendererMode::Emulated) {
    return event;
  }

  event.event_index = ++command_buffer_event_count_;
  event.function_address = function_address;
  event.function_name = function_name;
  event.link_register = ctx.lr;
  event.r3 = ctx.r3.u32;
  event.r4 = ctx.r4.u32;
  event.r5 = ctx.r5.u32;
  event.r6 = ctx.r6.u32;
  event.r7 = ctx.r7.u32;
  event.r8 = ctx.r8.u32;
  event.r9 = ctx.r9.u32;
  event.r10 = ctx.r10.u32;
  event.r28 = ctx.r28.u32;
  event.r29 = ctx.r29.u32;
  event.r30 = ctx.r30.u32;
  event.r31 = ctx.r31.u32;
  event.command_buffer_object = ctx.r3.u32;
  if (event.command_buffer_object) {
    event.write_begin = ReadGuestU32(event.command_buffer_object + 48);
    event.write_limit_begin = ReadGuestU32(event.command_buffer_object + 56);
  }
  return event;
}

void NativeRenderer::OnCommandBufferEventEnd(CommandBufferEventInfo& event,
                                             PPCContext& ctx) {
  if (!event.event_index) {
    return;
  }

  event.return_value = ctx.r3.u32;
  if (event.command_buffer_object) {
    event.write_end = ReadGuestU32(event.command_buffer_object + 48);
    event.write_limit_end = ReadGuestU32(event.command_buffer_object + 56);
  }

  if (ShaderRecordProbesEnabled() &&
      IsShaderRecordProbeAddress(event.function_address) && EnsureBackend()) {
    backend_->SubmitShaderRecordProbe(BuildShaderRecordProbe(event));
  }

  if (!config_.verbose || !ShouldLogHighFrequencyEvent(event.event_index)) {
    return;
  }

  REXLOG_INFO(
      "BO2 native renderer render hook #{} {}({:#010x}) lr={:#010x} "
      "r3={:#010x} r4={:#010x} r5={:#010x} r6={:#010x} "
      "r7={:#010x} r8={:#010x} r9={:#010x} r10={:#010x} "
      "write={:#010x}->{:#010x} limit={:#010x}->{:#010x} ret={:#010x}",
      event.event_index, event.function_name, event.function_address,
      static_cast<uint32_t>(event.link_register), event.r3, event.r4, event.r5,
      event.r6, event.r7, event.r8, event.r9, event.r10, event.write_begin,
      event.write_end, event.write_limit_begin, event.write_limit_end,
      event.return_value);
}

void NativeRenderer::OnPM4Packet(PM4PacketInfo packet) {
  if (!EnsureBackend()) {
    return;
  }

  packet.event_index = ++pm4_packet_count_;
  backend_->SubmitPM4Packet(packet);
}

void NativeRenderer::OnPM4Draw(PM4DrawInfo draw) {
  if (!EnsureBackend()) {
    return;
  }

  draw.event_index = ++pm4_draw_count_;
  backend_->SubmitPM4Draw(draw);

  RenderCommand command{};
  command.type = RenderCommandType::DrawIndexed;
  command.sequence = draw.event_index;
  command.guest_address = draw.packet_ptr;
  command.arg0 = draw.index_count;
  command.arg1 = draw.primitive_type;
  command.arg2 = draw.source_select;
  command.arg3 = draw.indexed ? 1 : 0;
  backend_->SubmitRenderCommand(command);
}

void NativeRenderer::OnPM4Shader(PM4ShaderInfo shader) {
  if (!EnsureBackend()) {
    return;
  }

  shader.event_index = ++pm4_shader_count_;
  backend_->SubmitPM4Shader(shader);

  RenderCommand command{};
  command.type = RenderCommandType::BindShader;
  command.sequence = shader.event_index;
  command.guest_address = shader.guest_address;
  command.arg0 = shader.shader_type;
  command.arg1 = static_cast<uint32_t>(shader.shader_hash);
  command.arg2 = static_cast<uint32_t>(shader.shader_hash >> 32);
  command.arg3 = shader.dword_count;
  backend_->SubmitRenderCommand(command);
}

void NativeRenderer::OnPM4Constants(PM4ConstantInfo constants) {
  if (!EnsureBackend()) {
    return;
  }

  constants.event_index = ++pm4_constant_count_;
  backend_->SubmitPM4Constants(constants);
}

void NativeRenderer::OnPM4Swap(PM4SwapInfo swap) {
  if (!EnsureBackend()) {
    return;
  }

  swap.event_index = ++pm4_swap_count_;
  backend_->SubmitPM4Swap(swap);
}

void NativeRenderer::Shutdown() {
  if (backend_) {
    backend_->Shutdown();
    backend_.reset();
  }
  configured_ = false;
}

bool NativeRenderer::EnsureBackend() {
  if (!configured_) {
    ConfigureForApp("unknown");
  }
  if (config_.backend == RendererBackendKind::None) {
    return false;
  }
  if (backend_) {
    return true;
  }
  switch (config_.backend) {
    case RendererBackendKind::D3D12Live:
      backend_ = std::make_unique<D3D12LiveRendererBackend>();
      break;
    case RendererBackendKind::NullDebug:
      backend_ = std::make_unique<NullRendererBackend>();
      break;
    case RendererBackendKind::None:
      return false;
  }
  resources_.Reset();
  shaders_.Reset();
  textures_.Reset();
  buffers_.Reset();
  return backend_->Initialize(config_);
}

VdSwapInfo NativeRenderer::CaptureVdSwap(PPCContext& ctx, uint8_t* base) const {
  VdSwapInfo swap{};
  swap.command_buffer = ReadGuestArg(ctx, base, 0);
  swap.fetch_constant = ReadGuestArg(ctx, base, 1);
  swap.writeback = ReadGuestArg(ctx, base, 2);
  swap.system_command_buffer = ReadGuestArg(ctx, base, 3);
  swap.system_command_buffer_token = ReadGuestArg(ctx, base, 4);
  swap.frontbuffer_ptr = ReadGuestArg(ctx, base, 5);
  swap.texture_format_ptr = ReadGuestArg(ctx, base, 6);
  swap.color_space_ptr = ReadGuestArg(ctx, base, 7);
  swap.width_ptr = ReadGuestArg(ctx, base, 8);
  swap.height_ptr = ReadGuestArg(ctx, base, 9);
  return swap;
}

CommandBufferSnapshot NativeRenderer::CaptureCommandBufferSnapshot(
    const VdSwapInfo& swap) const {
  CommandBufferSnapshot snapshot{};
  snapshot.frame_index = swap.frame_index;
  snapshot.command_buffer = swap.command_buffer;
  snapshot.dword_count = CommandBufferSnapshot::kMaxDwords;

  if (!swap.command_buffer) {
    return snapshot;
  }

  for (std::size_t i = 0; i < snapshot.dwords.size(); ++i) {
    snapshot.dwords[i] = ReadGuestU32(swap.command_buffer + static_cast<uint32_t>(i * 4));
  }

  for (std::size_t i = 0; i + 4 < snapshot.dwords.size(); ++i) {
    const uint32_t packet = snapshot.dwords[i];
    if (!IsType3Packet(packet, rex::graphics::xenos::PM4_XE_SWAP)) {
      continue;
    }

    snapshot.has_xe_swap = true;
    snapshot.xe_swap_dword_offset = static_cast<uint32_t>(i);
    snapshot.xe_swap_packet = packet;
    snapshot.swap_signature = snapshot.dwords[i + 1];
    snapshot.frontbuffer_physical = snapshot.dwords[i + 2];
    snapshot.width = snapshot.dwords[i + 3];
    snapshot.height = snapshot.dwords[i + 4];

    if (snapshot.swap_signature != rex::graphics::xenos::kSwapSignature) {
      REXLOG_WARN(
          "BO2 native renderer unexpected swap signature frame={} signature={:#010x}",
          snapshot.frame_index, snapshot.swap_signature);
    }
    break;
  }

  return snapshot;
}

void NativeRenderer::CaptureDrawPacketWrites(DrawPacketCandidateInfo& draw) const {
  if (!draw.command_buffer_object || !draw.write_begin) {
    return;
  }

  draw.write_end = ReadGuestU32(draw.command_buffer_object + 48);
  if (draw.write_end <= draw.write_begin) {
    return;
  }

  const uint32_t byte_count = draw.write_end - draw.write_begin;
  uint32_t dword_count = byte_count / 4;
  if (dword_count > DrawPacketCandidateInfo::kMaxDwords) {
    dword_count = DrawPacketCandidateInfo::kMaxDwords;
    draw.truncated = true;
  }
  draw.dword_count = dword_count;

  for (uint32_t i = 0; i < dword_count; ++i) {
    draw.dwords[i] = ReadGuestU32(draw.write_begin + 4 + i * 4);
  }

  for (uint32_t i = 0; i + 1 < draw.dword_count; ++i) {
    const uint32_t packet = draw.dwords[i];
    if (!IsType3Packet(packet, rex::graphics::xenos::PM4_DRAW_INDX_2)) {
      continue;
    }

    const uint32_t initiator = draw.dwords[i + 1];
    draw.has_draw_indx_2 = true;
    draw.draw_packet_dword_offset = i;
    draw.draw_packet = packet;
    draw.draw_initiator = initiator;
    draw.index_count = initiator >> 16;
    draw.primitive_type = initiator & 0x3F;
    draw.source_select = (initiator >> 6) & 0x3;
    draw.index_32bit = ((initiator >> 11) & 0x1) != 0;
    break;
  }
}

}  // namespace bo2::native
