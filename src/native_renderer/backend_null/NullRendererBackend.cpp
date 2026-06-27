#include "NullRendererBackend.h"

#include <rex/logging.h>

#include "../DebugRenderLog.h"

namespace bo2::native {

bool NullRendererBackend::Initialize(const RendererConfig& config) {
  verbose_ = config.verbose;
  app_name_ = config.app_name;
  last_error_.clear();
  REXLOG_INFO("BO2 native renderer null backend initialized for {}", app_name_);
  return true;
}

void NullRendererBackend::Shutdown() {
  REXLOG_INFO("BO2 native renderer null backend shutdown for {}", app_name_);
}

void NullRendererBackend::BeginFrame(uint64_t frame_index) {
  if (verbose_ && ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO("BO2 native renderer frame {} begin", frame_index);
  }
}

void NullRendererBackend::SubmitVdSwap(uint64_t frame_index, const VdSwapInfo& swap) {
  if (verbose_ && ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO(
        "BO2 native renderer VdSwap frame={} cmd={:#010x} fetch={:#010x} "
        "frontbuffer_ptr={:#010x} width_ptr={:#010x} height_ptr={:#010x}",
        frame_index, swap.command_buffer, swap.fetch_constant, swap.frontbuffer_ptr,
        swap.width_ptr, swap.height_ptr);
  }
}

void NullRendererBackend::SubmitCommandBufferSnapshot(
    uint64_t frame_index, const CommandBufferSnapshot& snapshot) {
  if (!verbose_ || !ShouldLogHighFrequencyEvent(frame_index)) {
    return;
  }

  if (snapshot.has_xe_swap) {
    REXLOG_INFO(
        "BO2 native renderer present packet frame={} cmd={:#010x} "
        "packet_dword={} frontbuffer_phys={:#010x} size={}x{}",
        frame_index, snapshot.command_buffer, snapshot.xe_swap_dword_offset,
        snapshot.frontbuffer_physical, snapshot.width, snapshot.height);
    return;
  }

  REXLOG_WARN(
      "BO2 native renderer present packet missing frame={} cmd={:#010x} "
      "first_dwords={:#010x} {:#010x} {:#010x} {:#010x}",
      frame_index, snapshot.command_buffer, snapshot.dwords[0], snapshot.dwords[1],
      snapshot.dwords[2], snapshot.dwords[3]);
}

void NullRendererBackend::EndFrame(uint64_t frame_index) {
  if (verbose_ && ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO("BO2 native renderer frame {} end", frame_index);
  }
}

}  // namespace bo2::native
