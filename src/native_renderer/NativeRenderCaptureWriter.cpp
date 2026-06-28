#include "NativeRenderCaptureWriter.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(native_renderer_capture_path, "", "Renderer",
                      "JSONL capture file for native renderer PM4 events");
REXCVAR_DEFINE_UINT64(
    native_renderer_capture_limit, 0, "Renderer",
    "Maximum native renderer capture events, or 0 for unlimited");
REXCVAR_DEFINE_UINT32(native_renderer_capture_flush_interval, 1024, "Renderer",
                      "Native renderer capture events between file flushes");

namespace bo2::native {

namespace {

std::string EscapeJson(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(ch) < 0x20) {
        std::ostringstream os;
        os << "\\u" << std::hex << std::uppercase << std::setw(4)
           << std::setfill('0')
           << static_cast<int>(static_cast<unsigned char>(ch));
        out += os.str();
      } else {
        out += ch;
      }
      break;
    }
  }
  return out;
}

std::string HexValue(uint64_t value, int width) {
  std::ostringstream os;
  os << "0x" << std::hex << std::uppercase << std::setw(width)
     << std::setfill('0') << value;
  return os.str();
}

const char *RenderCommandTypeName(RenderCommandType type) {
  switch (type) {
  case RenderCommandType::BeginFrame:
    return "BeginFrame";
  case RenderCommandType::EndFrame:
    return "EndFrame";
  case RenderCommandType::Present:
    return "Present";
  case RenderCommandType::BindShader:
    return "BindShader";
  case RenderCommandType::BindTexture:
    return "BindTexture";
  case RenderCommandType::SetRenderTarget:
    return "SetRenderTarget";
  case RenderCommandType::DrawIndexed:
    return "DrawIndexed";
  case RenderCommandType::Unsupported:
    return "Unsupported";
  }
  return "Unknown";
}

} // namespace

bool NativeRenderCaptureWriter::Initialize(const RendererConfig &config) {
  Shutdown();

  app_name_ = config.app_name;
  event_limit_ = REXCVAR_GET(native_renderer_capture_limit);
  flush_interval_ = std::max<uint32_t>(
      1, REXCVAR_GET(native_renderer_capture_flush_interval));

  const std::string configured_path = REXCVAR_GET(native_renderer_capture_path);
  if (configured_path.empty()) {
    return true;
  }

  path_ = std::filesystem::absolute(std::filesystem::path(configured_path));
  std::error_code ec;
  const auto parent = path_.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      REXLOG_ERROR(
          "BO2 native renderer could not create capture directory {}: {}",
          parent.string(), ec.message());
      return false;
    }
  }

  file_.open(path_, std::ios::out | std::ios::trunc);
  if (!file_) {
    REXLOG_ERROR("BO2 native renderer could not open capture {}",
                 path_.string());
    return false;
  }

  enabled_ = true;
  limited_ = false;
  event_count_ = 0;

  if (BeginEvent("capture_start")) {
    WriteStringField("app", app_name_);
    WriteStringField("mode", ToString(config.mode));
    WriteStringField("backend", ToString(config.backend));
    WriteU64Field("limit", event_limit_);
    EndEvent();
  }

  REXLOG_INFO(
      "BO2 native renderer capture enabled path={} limit={} flush_interval={}",
      path_.string(), event_limit_, flush_interval_);
  return true;
}

void NativeRenderCaptureWriter::Shutdown() {
  std::scoped_lock lock(mutex_);
  if (file_.is_open()) {
    file_.flush();
    file_.close();
  }
  enabled_ = false;
  limited_ = false;
  event_count_ = 0;
  event_limit_ = 0;
  path_.clear();
}

bool NativeRenderCaptureWriter::BeginEvent(std::string_view type) {
  if (!enabled_ || !file_) {
    return false;
  }
  if (event_limit_ && event_count_ >= event_limit_) {
    if (!limited_) {
      limited_ = true;
      file_.flush();
      REXLOG_WARN("BO2 native renderer capture reached event limit {}",
                  event_limit_);
    }
    return false;
  }

  ++event_count_;
  field_written_ = false;
  file_ << '{';
  WriteU64Field("seq", event_count_);
  WriteStringField("type", type);
  return true;
}

void NativeRenderCaptureWriter::EndEvent() {
  file_ << "}\n";
  MaybeFlush();
}

void NativeRenderCaptureWriter::MaybeFlush() {
  if (flush_interval_ && (event_count_ % flush_interval_) == 0) {
    file_.flush();
  }
}

void NativeRenderCaptureWriter::WriteFieldPrefix(std::string_view name) {
  if (field_written_) {
    file_ << ',';
  }
  field_written_ = true;
  file_ << '"' << name << "\":";
}

void NativeRenderCaptureWriter::WriteStringField(std::string_view name,
                                                 std::string_view value) {
  WriteFieldPrefix(name);
  file_ << '"' << EscapeJson(value) << '"';
}

void NativeRenderCaptureWriter::WriteBoolField(std::string_view name,
                                               bool value) {
  WriteFieldPrefix(name);
  file_ << (value ? "true" : "false");
}

void NativeRenderCaptureWriter::WriteU64Field(std::string_view name,
                                              uint64_t value) {
  WriteFieldPrefix(name);
  file_ << value;
}

void NativeRenderCaptureWriter::WriteHex32Field(std::string_view name,
                                                uint32_t value) {
  WriteStringField(name, HexValue(value, 8));
}

void NativeRenderCaptureWriter::WriteHex64Field(std::string_view name,
                                                uint64_t value) {
  WriteStringField(name, HexValue(value, 16));
}

void NativeRenderCaptureWriter::WriteHexSizeField(std::string_view name,
                                                  uintptr_t value) {
  WriteStringField(name, HexValue(static_cast<uint64_t>(value),
                                  sizeof(uintptr_t) == 8 ? 16 : 8));
}

void NativeRenderCaptureWriter::WriteBeginFrame(uint64_t frame_index) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("begin_frame")) {
    return;
  }
  WriteU64Field("frame", frame_index);
  EndEvent();
}

void NativeRenderCaptureWriter::WriteEndFrame(uint64_t frame_index) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("end_frame")) {
    return;
  }
  WriteU64Field("frame", frame_index);
  EndEvent();
}

void NativeRenderCaptureWriter::WriteVdSwap(uint64_t frame_index,
                                            const VdSwapInfo &swap) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("vd_swap")) {
    return;
  }
  WriteU64Field("frame", frame_index);
  WriteHex32Field("command_buffer", swap.command_buffer);
  WriteHex32Field("fetch_constant", swap.fetch_constant);
  WriteHex32Field("writeback", swap.writeback);
  WriteHex32Field("system_command_buffer", swap.system_command_buffer);
  WriteHex32Field("system_command_buffer_token",
                  swap.system_command_buffer_token);
  WriteHex32Field("frontbuffer_ptr", swap.frontbuffer_ptr);
  WriteHex32Field("texture_format_ptr", swap.texture_format_ptr);
  WriteHex32Field("color_space_ptr", swap.color_space_ptr);
  WriteHex32Field("width_ptr", swap.width_ptr);
  WriteHex32Field("height_ptr", swap.height_ptr);
  EndEvent();
}

void NativeRenderCaptureWriter::WriteCommandBufferSnapshot(
    uint64_t frame_index, const CommandBufferSnapshot &snapshot) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("present_snapshot")) {
    return;
  }
  WriteU64Field("frame", frame_index);
  WriteHex32Field("command_buffer", snapshot.command_buffer);
  WriteU64Field("dword_count", snapshot.dword_count);
  WriteBoolField("has_xe_swap", snapshot.has_xe_swap);
  WriteU64Field("xe_swap_dword_offset", snapshot.xe_swap_dword_offset);
  WriteHex32Field("xe_swap_packet", snapshot.xe_swap_packet);
  WriteHex32Field("swap_signature", snapshot.swap_signature);
  WriteHex32Field("frontbuffer_physical", snapshot.frontbuffer_physical);
  WriteU64Field("width", snapshot.width);
  WriteU64Field("height", snapshot.height);
  WriteFieldPrefix("dwords");
  file_ << '[';
  for (uint32_t i = 0; i < snapshot.dword_count && i < snapshot.dwords.size();
       ++i) {
    if (i) {
      file_ << ',';
    }
    file_ << '"' << HexValue(snapshot.dwords[i], 8) << '"';
  }
  file_ << ']';
  EndEvent();
}

void NativeRenderCaptureWriter::WriteDrawPacketCandidate(
    const DrawPacketCandidateInfo &draw) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("draw_packet_candidate")) {
    return;
  }
  WriteU64Field("event", draw.event_index);
  WriteStringField("function", draw.function_name);
  WriteHex32Field("function_address", draw.function_address);
  WriteHex64Field("link_register", draw.link_register);
  WriteHex32Field("command_buffer_object", draw.command_buffer_object);
  WriteHex32Field("write_begin", draw.write_begin);
  WriteHex32Field("write_end", draw.write_end);
  WriteHex32Field("write_limit", draw.write_limit);
  WriteU64Field("dword_count", draw.dword_count);
  WriteBoolField("truncated", draw.truncated);
  WriteBoolField("has_draw_indx_2", draw.has_draw_indx_2);
  WriteU64Field("draw_packet_dword_offset", draw.draw_packet_dword_offset);
  WriteHex32Field("draw_packet", draw.draw_packet);
  WriteHex32Field("draw_initiator", draw.draw_initiator);
  WriteU64Field("index_count", draw.index_count);
  WriteU64Field("primitive_type", draw.primitive_type);
  WriteU64Field("source_select", draw.source_select);
  WriteBoolField("index_32bit", draw.index_32bit);
  EndEvent();
}

void NativeRenderCaptureWriter::WritePM4Packet(const PM4PacketInfo &packet) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("pm4_packet")) {
    return;
  }
  WriteU64Field("event", packet.event_index);
  WriteU64Field("opcode", packet.opcode);
  WriteHex32Field("packet", packet.packet);
  WriteU64Field("payload_dword_count", packet.payload_dword_count);
  WriteHex32Field("packet_ptr", packet.packet_ptr);
  WriteHex32Field("buffer_ptr", packet.buffer_ptr);
  WriteU64Field("packet_offset", packet.packet_offset);
  WriteU64Field("first_dword_count", packet.first_dword_count);
  WriteFieldPrefix("first_dwords");
  file_ << '[';
  for (uint32_t i = 0;
       i < packet.first_dword_count && i < packet.first_dwords.size(); ++i) {
    if (i) {
      file_ << ',';
    }
    file_ << '"' << HexValue(packet.first_dwords[i], 8) << '"';
  }
  file_ << ']';
  EndEvent();
}

void NativeRenderCaptureWriter::WritePM4Draw(const PM4DrawInfo &draw) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("pm4_draw")) {
    return;
  }
  WriteU64Field("event", draw.event_index);
  WriteStringField("opcode_name", draw.opcode_name);
  WriteU64Field("opcode", draw.opcode);
  WriteHex32Field("packet", draw.packet);
  WriteHex32Field("packet_ptr", draw.packet_ptr);
  WriteHex32Field("buffer_ptr", draw.buffer_ptr);
  WriteU64Field("packet_offset", draw.packet_offset);
  WriteU64Field("index_count", draw.index_count);
  WriteU64Field("primitive_type", draw.primitive_type);
  WriteU64Field("source_select", draw.source_select);
  WriteBoolField("indexed", draw.indexed);
  WriteHex32Field("index_base", draw.index_base);
  WriteU64Field("index_length", draw.index_length);
  WriteU64Field("index_buffer_count", draw.index_buffer_count);
  WriteU64Field("index_format", draw.index_format);
  WriteU64Field("index_endianness", draw.index_endianness);
  WriteU64Field("major_mode", draw.major_mode);
  WriteBoolField("explicit_major_mode", draw.explicit_major_mode);
  WriteHex32Field("viz_query_condition", draw.viz_query_condition);
  WriteHex64Field("vertex_shader_hash", draw.vertex_shader_hash);
  WriteHex64Field("pixel_shader_hash", draw.pixel_shader_hash);
  EndEvent();
}

void NativeRenderCaptureWriter::WritePM4Shader(const PM4ShaderInfo &shader) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("pm4_shader")) {
    return;
  }
  WriteU64Field("event", shader.event_index);
  WriteU64Field("opcode", shader.opcode);
  WriteHex32Field("packet", shader.packet);
  WriteHex32Field("packet_ptr", shader.packet_ptr);
  WriteHex32Field("buffer_ptr", shader.buffer_ptr);
  WriteU64Field("packet_offset", shader.packet_offset);
  WriteU64Field("shader_type", shader.shader_type);
  WriteBoolField("embedded", shader.embedded);
  WriteHex32Field("guest_address", shader.guest_address);
  WriteHexSizeField("host_address", shader.host_address);
  WriteU64Field("dword_count", shader.dword_count);
  WriteHex64Field("shader_hash", shader.shader_hash);
  EndEvent();
}

void NativeRenderCaptureWriter::WritePM4Constants(
    const PM4ConstantInfo &constants) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("pm4_constants")) {
    return;
  }
  WriteU64Field("event", constants.event_index);
  WriteU64Field("opcode", constants.opcode);
  WriteHex32Field("packet", constants.packet);
  WriteHex32Field("packet_ptr", constants.packet_ptr);
  WriteHex32Field("buffer_ptr", constants.buffer_ptr);
  WriteU64Field("packet_offset", constants.packet_offset);
  WriteHex32Field("address", constants.address);
  WriteHex32Field("offset_type", constants.offset_type);
  WriteU64Field("constant_type", constants.type);
  WriteU64Field("index", constants.index);
  WriteU64Field("dword_count", constants.dword_count);
  WriteU64Field("payload_dword_count", constants.payload_dword_count);
  WriteBoolField("payload_truncated", constants.payload_truncated);
  WriteBoolField("payload_missing", constants.payload_missing);
  WriteFieldPrefix("dwords");
  file_ << '[';
  for (uint32_t i = 0;
       i < constants.payload_dword_count && i < constants.payload_dwords.size();
       ++i) {
    if (i) {
      file_ << ',';
    }
    file_ << '"' << HexValue(constants.payload_dwords[i], 8) << '"';
  }
  file_ << ']';
  EndEvent();
}

void NativeRenderCaptureWriter::WritePM4Swap(const PM4SwapInfo &swap) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("pm4_swap")) {
    return;
  }
  WriteU64Field("event", swap.event_index);
  WriteU64Field("opcode", swap.opcode);
  WriteHex32Field("packet", swap.packet);
  WriteHex32Field("packet_ptr", swap.packet_ptr);
  WriteHex32Field("buffer_ptr", swap.buffer_ptr);
  WriteU64Field("packet_offset", swap.packet_offset);
  WriteHex32Field("frontbuffer_ptr", swap.frontbuffer_ptr);
  WriteU64Field("width", swap.width);
  WriteU64Field("height", swap.height);
  WriteU64Field("frame_counter", swap.frame_counter);
  EndEvent();
}

void NativeRenderCaptureWriter::WriteRenderCommand(
    const RenderCommand &command) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("render_command")) {
    return;
  }
  WriteU64Field("sequence", command.sequence);
  WriteStringField("command", RenderCommandTypeName(command.type));
  WriteHex32Field("guest_address", command.guest_address);
  WriteU64Field("arg0", command.arg0);
  WriteU64Field("arg1", command.arg1);
  WriteU64Field("arg2", command.arg2);
  WriteU64Field("arg3", command.arg3);
  EndEvent();
}

} // namespace bo2::native
