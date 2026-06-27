#include "NativeRenderer.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

#include <rex/cvar.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/function.h>
#include <rex/system/kernel_state.h>

#include "DebugRenderLog.h"
#include "RendererBackend.h"
#include "backend_null/NullRendererBackend.h"

REXCVAR_DEFINE_STRING(native_renderer_mode, "emulated", "Renderer",
                      "Renderer mode: emulated, native, native_null");
REXCVAR_DEFINE_BOOL(native_renderer_verbose, true, "Renderer",
                    "Enable verbose native renderer logging");

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
  }
  return RendererBackendKind::None;
}

uint32_t ReadGuestArg(PPCContext& ctx, uint8_t* base, std::size_t arg) {
  return static_cast<uint32_t>(
      rex::ppc::ArgTranslator::GetIntegerArgumentValue(ctx, base, arg));
}

uint32_t ReadGuestU32(uint32_t address) {
  if (!address) {
    return 0;
  }
  const auto* ptr = REX_KERNEL_MEMORY()->TranslateVirtual<const uint32_t*>(address);
  return rex::memory::load_and_swap<uint32_t>(ptr);
}

bool IsType3Packet(uint32_t packet, rex::graphics::xenos::Type3Opcode opcode) {
  return ((packet >> 30) == 3) &&
         (((packet >> 8) & 0x7F) == static_cast<uint32_t>(opcode));
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
  }
  return "unknown";
}

const char* ToString(RendererBackendKind backend) {
  switch (backend) {
    case RendererBackendKind::None:
      return "none";
    case RendererBackendKind::NullDebug:
      return "null_debug";
  }
  return "unknown";
}

RendererMode ParseRendererMode(std::string mode) {
  mode = Normalize(std::move(mode));
  if (mode == "native") {
    return RendererMode::Native;
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
  configured_ = true;

  REXLOG_INFO("BO2 native renderer mode={} backend={} verbose={} app={}",
              ToString(config_.mode), ToString(config_.backend), config_.verbose,
              config_.app_name);

  if (ShouldInstallHooks()) {
    EnsureBackend();
  }
}

bool NativeRenderer::ShouldInstallHooks() const {
  return config_.mode != RendererMode::Emulated;
}

bool NativeRenderer::ShouldSuppressEmulatedPresent() const {
  return config_.mode == RendererMode::NativeNull;
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

}  // namespace bo2::native
