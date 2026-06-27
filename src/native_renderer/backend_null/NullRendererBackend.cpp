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

void NullRendererBackend::SubmitDrawPacketCandidate(
    const DrawPacketCandidateInfo& draw) {
  if (!verbose_ || !ShouldLogHighFrequencyEvent(draw.event_index)) {
    return;
  }

  if (draw.has_draw_indx_2) {
    REXLOG_INFO(
        "BO2 native renderer draw candidate #{} {}({:#010x}) "
        "cmd_obj={:#010x} writes={:#010x}->{:#010x} dwords={} "
        "PM4_DRAW_INDX_2@{} indices={} prim={} src={} index32={}",
        draw.event_index, draw.function_name, draw.function_address,
        draw.command_buffer_object, draw.write_begin, draw.write_end, draw.dword_count,
        draw.draw_packet_dword_offset, draw.index_count, draw.primitive_type,
        draw.source_select, draw.index_32bit);
    return;
  }

  REXLOG_INFO(
      "BO2 native renderer draw candidate #{} {}({:#010x}) lr={:#010x} "
      "r3={:#010x} r4={:#010x} r5={:#010x} r6={:#010x} r7={:#010x} r8={:#010x} "
      "cmd_obj={:#010x} writes={:#010x}->{:#010x} dwords={} truncated={}",
      draw.event_index, draw.function_name, draw.function_address,
      static_cast<uint32_t>(draw.link_register), draw.r3, draw.r4, draw.r5, draw.r6,
      draw.r7, draw.r8, draw.command_buffer_object, draw.write_begin, draw.write_end,
      draw.dword_count, draw.truncated);
}

void NullRendererBackend::EndFrame(uint64_t frame_index) {
  if (verbose_ && ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO("BO2 native renderer frame {} end", frame_index);
  }
}

}  // namespace bo2::native
