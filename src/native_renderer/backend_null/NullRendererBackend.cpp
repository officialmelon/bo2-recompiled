#include "NullRendererBackend.h"

#include <rex/logging.h>

#include "../DebugRenderLog.h"

namespace bo2::native {

namespace {

const char *ShaderTypeName(uint32_t shader_type) {
  switch (shader_type) {
  case 0:
    return "vertex";
  case 1:
    return "pixel";
  default:
    return "unknown";
  }
}

} // namespace

bool NullRendererBackend::Initialize(const RendererConfig &config) {
  verbose_ = config.verbose;
  app_name_ = config.app_name;
  last_error_.clear();
  if (!capture_.Initialize(config)) {
    last_error_ = "failed to initialize native renderer capture writer";
    return false;
  }
  REXLOG_INFO("BO2 native renderer null backend initialized for {}", app_name_);
  return true;
}

void NullRendererBackend::Shutdown() {
  capture_.Shutdown();
  REXLOG_INFO("BO2 native renderer null backend shutdown for {}", app_name_);
}

void NullRendererBackend::BeginFrame(uint64_t frame_index) {
  capture_.WriteBeginFrame(frame_index);
  if (verbose_ && ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO("BO2 native renderer frame {} begin", frame_index);
  }
}

void NullRendererBackend::SubmitVdSwap(uint64_t frame_index,
                                       const VdSwapInfo &swap) {
  capture_.WriteVdSwap(frame_index, swap);
  if (verbose_ && ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO(
        "BO2 native renderer VdSwap frame={} cmd={:#010x} fetch={:#010x} "
        "frontbuffer_ptr={:#010x} width_ptr={:#010x} height_ptr={:#010x}",
        frame_index, swap.command_buffer, swap.fetch_constant,
        swap.frontbuffer_ptr, swap.width_ptr, swap.height_ptr);
  }
}

void NullRendererBackend::SubmitCommandBufferSnapshot(
    uint64_t frame_index, const CommandBufferSnapshot &snapshot) {
  capture_.WriteCommandBufferSnapshot(frame_index, snapshot);
  if (!verbose_ || !ShouldLogHighFrequencyEvent(frame_index)) {
    return;
  }

  if (snapshot.has_xe_swap) {
    REXLOG_INFO("BO2 native renderer present packet frame={} cmd={:#010x} "
                "packet_dword={} frontbuffer_phys={:#010x} size={}x{}",
                frame_index, snapshot.command_buffer,
                snapshot.xe_swap_dword_offset, snapshot.frontbuffer_physical,
                snapshot.width, snapshot.height);
    return;
  }

  REXLOG_WARN(
      "BO2 native renderer present packet missing frame={} cmd={:#010x} "
      "first_dwords={:#010x} {:#010x} {:#010x} {:#010x}",
      frame_index, snapshot.command_buffer, snapshot.dwords[0],
      snapshot.dwords[1], snapshot.dwords[2], snapshot.dwords[3]);
}

void NullRendererBackend::SubmitDrawPacketCandidate(
    const DrawPacketCandidateInfo &draw) {
  capture_.WriteDrawPacketCandidate(draw);
  if (!verbose_ || !ShouldLogHighFrequencyEvent(draw.event_index)) {
    return;
  }

  if (draw.has_draw_indx_2) {
    REXLOG_INFO("BO2 native renderer draw candidate #{} {}({:#010x}) "
                "cmd_obj={:#010x} writes={:#010x}->{:#010x} dwords={} "
                "PM4_DRAW_INDX_2@{} indices={} prim={} src={} index32={}",
                draw.event_index, draw.function_name, draw.function_address,
                draw.command_buffer_object, draw.write_begin, draw.write_end,
                draw.dword_count, draw.draw_packet_dword_offset,
                draw.index_count, draw.primitive_type, draw.source_select,
                draw.index_32bit);
    return;
  }

  REXLOG_INFO(
      "BO2 native renderer draw candidate #{} {}({:#010x}) lr={:#010x} "
      "r3={:#010x} r4={:#010x} r5={:#010x} r6={:#010x} r7={:#010x} r8={:#010x} "
      "cmd_obj={:#010x} writes={:#010x}->{:#010x} dwords={} truncated={}",
      draw.event_index, draw.function_name, draw.function_address,
      static_cast<uint32_t>(draw.link_register), draw.r3, draw.r4, draw.r5,
      draw.r6, draw.r7, draw.r8, draw.command_buffer_object, draw.write_begin,
      draw.write_end, draw.dword_count, draw.truncated);
}

void NullRendererBackend::SubmitPM4Packet(const PM4PacketInfo &packet) {
  capture_.WritePM4Packet(packet);
  if (!verbose_ || !ShouldLogHighFrequencyEvent(packet.event_index)) {
    return;
  }

  REXLOG_INFO(
      "BO2 native renderer CP packet #{} opcode=0x{:02X} packet={:#010x} "
      "cmd={:#010x} offset={:#08x} payload={} dwords[{}]={:#010x} {:#010x} "
      "{:#010x} {:#010x}",
      packet.event_index, packet.opcode, packet.packet, packet.buffer_ptr,
      packet.packet_offset, packet.payload_dword_count,
      packet.first_dword_count, packet.first_dwords[0], packet.first_dwords[1],
      packet.first_dwords[2], packet.first_dwords[3]);
}

void NullRendererBackend::SubmitPM4Draw(const PM4DrawInfo &draw) {
  capture_.WritePM4Draw(draw);
  if (!verbose_ || !ShouldLogHighFrequencyEvent(draw.event_index)) {
    return;
  }

  REXLOG_INFO(
      "BO2 native renderer CP draw #{} {} packet={:#010x} cmd={:#010x} "
      "offset={:#08x} indices={} prim={} src={} indexed={} index_base={:#010x} "
      "index_len={} index_count={} index_format={} index_endian={} major={} "
      "explicit_major={} viz={:#010x} VS={:016X} PS={:016X}",
      draw.event_index,
      draw.opcode_name.empty() ? "PM4_DRAW" : draw.opcode_name, draw.packet_ptr,
      draw.buffer_ptr, draw.packet_offset, draw.index_count,
      draw.primitive_type, draw.source_select, draw.indexed, draw.index_base,
      draw.index_length, draw.index_buffer_count, draw.index_format,
      draw.index_endianness, draw.major_mode, draw.explicit_major_mode,
      draw.viz_query_condition, draw.vertex_shader_hash,
      draw.pixel_shader_hash);
}

void NullRendererBackend::SubmitPM4Shader(const PM4ShaderInfo &shader) {
  capture_.WritePM4Shader(shader);
  if (!verbose_ || !ShouldLogHighFrequencyEvent(shader.event_index)) {
    return;
  }

  REXLOG_INFO(
      "BO2 native renderer CP shader #{} type={} embedded={} guest={:#010x} "
      "host={:#018x} dwords={} hash={:016X} packet={:#010x}",
      shader.event_index, ShaderTypeName(shader.shader_type), shader.embedded,
      shader.guest_address, static_cast<uint64_t>(shader.host_address),
      shader.dword_count, shader.shader_hash, shader.packet_ptr);
}

void NullRendererBackend::SubmitPM4Constants(const PM4ConstantInfo &constants) {
  capture_.WritePM4Constants(constants);
  if (!verbose_ || !ShouldLogHighFrequencyEvent(constants.event_index)) {
    return;
  }

  REXLOG_INFO(
      "BO2 native renderer CP constants #{} opcode=0x{:02X} packet={:#010x} "
      "addr={:#010x} offset_type={:#010x} type={} index={} dwords={} "
      "payload={} truncated={} missing={}",
      constants.event_index, constants.opcode, constants.packet_ptr,
      constants.address, constants.offset_type, constants.type, constants.index,
      constants.dword_count, constants.payload_dword_count,
      constants.payload_truncated, constants.payload_missing);
}

void NullRendererBackend::SubmitPM4Swap(const PM4SwapInfo &swap) {
  capture_.WritePM4Swap(swap);
  if (!verbose_ || !ShouldLogHighFrequencyEvent(swap.event_index)) {
    return;
  }

  REXLOG_INFO("BO2 native renderer CP swap #{} frontbuffer={:#010x} size={}x{} "
              "frame_counter={} packet={:#010x}",
              swap.event_index, swap.frontbuffer_ptr, swap.width, swap.height,
              swap.frame_counter, swap.packet_ptr);
}

void NullRendererBackend::SubmitRenderCommand(const RenderCommand &command) {
  capture_.WriteRenderCommand(command);
  if (!verbose_ || !ShouldLogHighFrequencyEvent(command.sequence)) {
    return;
  }

  switch (command.type) {
  case RenderCommandType::BindShader:
    REXLOG_INFO(
        "BO2 native renderer command #{} BindShader stage={} hash={:08X}{:08X} "
        "dwords={} guest={:#010x}",
        command.sequence, command.arg0, command.arg2, command.arg1,
        command.arg3, command.guest_address);
    break;
  case RenderCommandType::DrawIndexed:
    REXLOG_INFO("BO2 native renderer command #{} DrawIndexed packet={:#010x} "
                "indices={} prim={} src={} index32={}",
                command.sequence, command.guest_address, command.arg0,
                command.arg1, command.arg2, command.arg3);
    break;
  default:
    REXLOG_WARN("BO2 native renderer unsupported command #{} type={}",
                command.sequence, static_cast<uint32_t>(command.type));
    break;
  }
}

void NullRendererBackend::EndFrame(uint64_t frame_index) {
  capture_.WriteEndFrame(frame_index);
  if (verbose_ && ShouldLogHighFrequencyEvent(frame_index)) {
    REXLOG_INFO("BO2 native renderer frame {} end", frame_index);
  }
}

} // namespace bo2::native
