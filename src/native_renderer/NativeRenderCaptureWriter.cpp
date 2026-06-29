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
  WriteU64Field("index_payload_byte_count", draw.index_payload_byte_count);
  WriteBoolField("index_payload_truncated", draw.index_payload_truncated);
  WriteBoolField("index_payload_missing", draw.index_payload_missing);
  WriteFieldPrefix("index_bytes");
  file_ << '[';
  const uint32_t index_copy_count = std::min<uint32_t>(
      draw.index_payload_byte_count, draw.index_bytes.size());
  for (uint32_t i = 0; i < index_copy_count; ++i) {
    if (i) {
      file_ << ',';
    }
    file_ << '"' << HexValue(draw.index_bytes[i], 2) << '"';
  }
  file_ << ']';
  WriteU64Field("vertex_fetch_count", draw.vertex_fetch_count);
  WriteBoolField("vertex_fetch_truncated", draw.vertex_fetch_truncated);
  WriteFieldPrefix("vertex_fetches");
  file_ << '[';
  const uint32_t vertex_fetch_count = std::min<uint32_t>(
      draw.vertex_fetch_count, draw.vertex_fetches.size());
  for (uint32_t i = 0; i < vertex_fetch_count; ++i) {
    if (i) {
      file_ << ',';
    }
    const VertexFetchInfo &fetch = draw.vertex_fetches[i];
    file_ << '{';
    bool first_fetch_field = true;
    auto fetch_prefix = [&]() {
      if (!first_fetch_field) {
        file_ << ',';
      }
      first_fetch_field = false;
    };
    auto fetch_u64 = [&](const char *name, uint64_t value) {
      fetch_prefix();
      file_ << '"' << name << "\":" << value;
    };
    auto fetch_bool = [&](const char *name, bool value) {
      fetch_prefix();
      file_ << '"' << name << "\":" << (value ? "true" : "false");
    };
    auto fetch_hex = [&](const char *name, uint64_t value, int width) {
      fetch_prefix();
      file_ << '"' << name << "\":\"" << HexValue(value, width) << '"';
    };
    fetch_u64("fetch_constant", fetch.fetch_constant);
    fetch_hex("dword_0", fetch.dword_0, 8);
    fetch_hex("dword_1", fetch.dword_1, 8);
    fetch_u64("type", fetch.type);
    fetch_hex("address", fetch.address, 8);
    fetch_hex("address_bytes", uint64_t(fetch.address) << 2, 8);
    fetch_u64("size_words", fetch.size);
    fetch_u64("size_bytes", uint64_t(fetch.size) << 2);
    fetch_u64("endian", fetch.endian);
    fetch_u64("stride_words", fetch.stride_words);
    fetch_u64("stride_bytes", uint64_t(fetch.stride_words) << 2);
    fetch_u64("attribute_count", fetch.attribute_count);
    fetch_u64("captured_attribute_count", fetch.captured_attribute_count);
    fetch_prefix();
    file_ << "\"attributes\":[";
    const uint32_t attribute_count = std::min<uint32_t>(
        fetch.captured_attribute_count, fetch.attributes.size());
    for (uint32_t j = 0; j < attribute_count; ++j) {
      if (j) {
        file_ << ',';
      }
      const VertexAttributeInfo &attribute = fetch.attributes[j];
      file_ << '{'
            << "\"data_format\":" << attribute.data_format
            << ",\"offset\":" << attribute.offset
            << ",\"offset_bytes\":" << (int64_t(attribute.offset) * 4)
            << ",\"stride\":" << attribute.stride
            << ",\"stride_bytes\":" << (uint64_t(attribute.stride) << 2)
            << ",\"exp_adjust\":" << attribute.exp_adjust
            << ",\"prefetch_count\":" << attribute.prefetch_count
            << ",\"signed_rf_mode\":" << attribute.signed_rf_mode
            << ",\"is_index_rounded\":"
            << (attribute.is_index_rounded ? "true" : "false")
            << ",\"is_signed\":" << (attribute.is_signed ? "true" : "false")
            << ",\"is_integer\":"
            << (attribute.is_integer ? "true" : "false") << '}';
    }
    file_ << ']';
    fetch_u64("payload_byte_count", fetch.payload_byte_count);
    fetch_bool("payload_truncated", fetch.payload_truncated);
    fetch_bool("payload_missing", fetch.payload_missing);
    fetch_prefix();
    file_ << "\"payload_bytes\":[";
    const uint32_t payload_count = std::min<uint32_t>(
        fetch.payload_byte_count, fetch.payload_bytes.size());
    for (uint32_t j = 0; j < payload_count; ++j) {
      if (j) {
        file_ << ',';
      }
      file_ << '"' << HexValue(fetch.payload_bytes[j], 2) << '"';
    }
    file_ << "]}";
  }
  file_ << ']';
  WriteU64Field("texture_fetch_count", draw.texture_fetch_count);
  WriteBoolField("texture_fetch_truncated", draw.texture_fetch_truncated);
  WriteFieldPrefix("texture_fetches");
  file_ << '[';
  const uint32_t texture_fetch_count = std::min<uint32_t>(
      draw.texture_fetch_count, draw.texture_fetches.size());
  for (uint32_t i = 0; i < texture_fetch_count; ++i) {
    if (i) {
      file_ << ',';
    }
    const TextureFetchInfo &fetch = draw.texture_fetches[i];
    file_ << '{';
    bool first_fetch_field = true;
    auto fetch_prefix = [&]() {
      if (!first_fetch_field) {
        file_ << ',';
      }
      first_fetch_field = false;
    };
    auto fetch_u64 = [&](const char *name, uint64_t value) {
      fetch_prefix();
      file_ << '"' << name << "\":" << value;
    };
    auto fetch_i64 = [&](const char *name, int64_t value) {
      fetch_prefix();
      file_ << '"' << name << "\":" << value;
    };
    auto fetch_bool = [&](const char *name, bool value) {
      fetch_prefix();
      file_ << '"' << name << "\":" << (value ? "true" : "false");
    };
    auto fetch_hex = [&](const char *name, uint64_t value, int width) {
      fetch_prefix();
      file_ << '"' << name << "\":\"" << HexValue(value, width) << '"';
    };
    fetch_u64("shader_type", fetch.shader_type);
    fetch_u64("binding_index", fetch.binding_index);
    fetch_u64("fetch_constant", fetch.fetch_constant);
    fetch_prefix();
    file_ << "\"dwords\":[";
    for (uint32_t j = 0; j < fetch.dwords.size(); ++j) {
      if (j) {
        file_ << ',';
      }
      file_ << '"' << HexValue(fetch.dwords[j], 8) << '"';
    }
    file_ << ']';
    fetch_u64("type", fetch.type);
    fetch_hex("base_address", fetch.base_address, 8);
    fetch_hex("base_address_bytes", fetch.base_address_bytes, 8);
    fetch_hex("mip_address", fetch.mip_address, 8);
    fetch_hex("mip_address_bytes", fetch.mip_address_bytes, 8);
    fetch_u64("pitch", fetch.pitch);
    fetch_bool("tiled", fetch.tiled);
    fetch_u64("format", fetch.format);
    fetch_u64("endian", fetch.endian);
    fetch_u64("request_size", fetch.request_size);
    fetch_bool("stacked", fetch.stacked);
    fetch_u64("width", fetch.width);
    fetch_u64("height", fetch.height);
    fetch_u64("depth_or_stack", fetch.depth_or_stack);
    fetch_u64("num_format", fetch.num_format);
    fetch_hex("swizzle", fetch.swizzle, 4);
    fetch_i64("exp_adjust", fetch.exp_adjust);
    fetch_u64("clamp_x", fetch.clamp_x);
    fetch_u64("clamp_y", fetch.clamp_y);
    fetch_u64("clamp_z", fetch.clamp_z);
    fetch_u64("mag_filter", fetch.mag_filter);
    fetch_u64("min_filter", fetch.min_filter);
    fetch_u64("mip_filter", fetch.mip_filter);
    fetch_u64("aniso_filter", fetch.aniso_filter);
    fetch_u64("arbitrary_filter", fetch.arbitrary_filter);
    fetch_u64("border_size", fetch.border_size);
    fetch_u64("vol_mag_filter", fetch.vol_mag_filter);
    fetch_u64("vol_min_filter", fetch.vol_min_filter);
    fetch_u64("mip_min_level", fetch.mip_min_level);
    fetch_u64("mip_max_level", fetch.mip_max_level);
    fetch_i64("lod_bias", fetch.lod_bias);
    fetch_i64("grad_exp_adjust_h", fetch.grad_exp_adjust_h);
    fetch_i64("grad_exp_adjust_v", fetch.grad_exp_adjust_v);
    fetch_u64("border_color", fetch.border_color);
    fetch_u64("force_bc_w_to_max", fetch.force_bc_w_to_max);
    fetch_u64("tri_clamp", fetch.tri_clamp);
    fetch_i64("aniso_bias", fetch.aniso_bias);
    fetch_u64("dimension", fetch.dimension);
    fetch_bool("packed_mips", fetch.packed_mips);
    fetch_u64("payload_byte_count", fetch.payload_byte_count);
    fetch_bool("payload_truncated", fetch.payload_truncated);
    fetch_bool("payload_missing", fetch.payload_missing);
    fetch_prefix();
    file_ << "\"payload_bytes\":[";
    const uint32_t payload_count = std::min<uint32_t>(
        fetch.payload_byte_count, fetch.payload_bytes.size());
    for (uint32_t j = 0; j < payload_count; ++j) {
      if (j) {
        file_ << ',';
      }
      file_ << '"' << HexValue(fetch.payload_bytes[j], 2) << '"';
    }
    file_ << "]}";
  }
  file_ << ']';
  WriteFieldPrefix("render_state");
  file_ << '{';
  const RenderStateInfo &state = draw.render_state;
  bool first_state_field = true;
  auto state_prefix = [&]() {
    if (!first_state_field) {
      file_ << ',';
    }
    first_state_field = false;
  };
  auto state_u64 = [&](const char *name, uint64_t value) {
    state_prefix();
    file_ << '"' << name << "\":" << value;
  };
  auto state_i64 = [&](const char *name, int64_t value) {
    state_prefix();
    file_ << '"' << name << "\":" << value;
  };
  auto state_bool = [&](const char *name, bool value) {
    state_prefix();
    file_ << '"' << name << "\":" << (value ? "true" : "false");
  };
  auto state_hex = [&](const char *name, uint64_t value, int width) {
    state_prefix();
    file_ << '"' << name << "\":\"" << HexValue(value, width) << '"';
  };
  auto state_hex_array = [&](const char *name, const auto &values, int width) {
    state_prefix();
    file_ << '"' << name << "\":[";
    for (uint32_t i = 0; i < values.size(); ++i) {
      if (i) {
        file_ << ',';
      }
      file_ << '"' << HexValue(values[i], width) << '"';
    }
    file_ << ']';
  };
  auto state_u64_array = [&](const char *name, const auto &values) {
    state_prefix();
    file_ << '"' << name << "\":[";
    for (uint32_t i = 0; i < values.size(); ++i) {
      if (i) {
        file_ << ',';
      }
      file_ << values[i];
    }
    file_ << ']';
  };
  auto state_i64_array = [&](const char *name, const auto &values) {
    state_prefix();
    file_ << '"' << name << "\":[";
    for (uint32_t i = 0; i < values.size(); ++i) {
      if (i) {
        file_ << ',';
      }
      file_ << static_cast<int64_t>(values[i]);
    }
    file_ << ']';
  };
  state_hex("rb_modecontrol", state.rb_modecontrol, 8);
  state_hex("rb_surface_info", state.rb_surface_info, 8);
  state_hex("rb_colorcontrol", state.rb_colorcontrol, 8);
  state_hex("rb_color_mask", state.rb_color_mask, 8);
  state_hex("rb_depthcontrol", state.rb_depthcontrol, 8);
  state_hex("rb_stencilrefmask", state.rb_stencilrefmask, 8);
  state_hex("rb_stencilrefmask_bf", state.rb_stencilrefmask_bf, 8);
  state_hex("rb_depth_info", state.rb_depth_info, 8);
  state_hex("rb_alpha_ref", state.rb_alpha_ref, 8);
  state_hex("pa_sc_screen_scissor_tl", state.pa_sc_screen_scissor_tl, 8);
  state_hex("pa_sc_screen_scissor_br", state.pa_sc_screen_scissor_br, 8);
  state_hex("pa_sc_window_offset", state.pa_sc_window_offset, 8);
  state_hex("pa_sc_window_scissor_tl", state.pa_sc_window_scissor_tl, 8);
  state_hex("pa_sc_window_scissor_br", state.pa_sc_window_scissor_br, 8);
  state_hex("pa_cl_clip_cntl", state.pa_cl_clip_cntl, 8);
  state_hex("pa_cl_vte_cntl", state.pa_cl_vte_cntl, 8);
  state_hex("pa_su_sc_mode_cntl", state.pa_su_sc_mode_cntl, 8);
  state_hex("pa_su_vtx_cntl", state.pa_su_vtx_cntl, 8);
  state_hex("sq_program_cntl", state.sq_program_cntl, 8);
  state_hex("sq_context_misc", state.sq_context_misc, 8);
  state_hex_array("viewport_registers", state.viewport_registers, 8);
  state_hex_array("rb_color_info", state.rb_color_info, 8);
  state_hex_array("rb_blendcontrol", state.rb_blendcontrol, 8);
  state_u64("surface_pitch", state.surface_pitch);
  state_u64("msaa_samples", state.msaa_samples);
  state_u64("depth_base", state.depth_base);
  state_u64("depth_format", state.depth_format);
  state_u64_array("color_base", state.color_base);
  state_u64_array("color_format", state.color_format);
  state_i64_array("color_exp_bias", state.color_exp_bias);
  state_bool("depth_test_enable", state.depth_test_enable);
  state_bool("depth_write_enable", state.depth_write_enable);
  state_bool("stencil_enable", state.stencil_enable);
  state_u64("depth_func", state.depth_func);
  state_u64("cull_mode", state.cull_mode);
  state_u64("fill_mode", state.fill_mode);
  state_u64("front_face", state.front_face);
  file_ << '}';
  WriteU64Field("major_mode", draw.major_mode);
  WriteBoolField("explicit_major_mode", draw.explicit_major_mode);
  WriteHex32Field("viz_query_condition", draw.viz_query_condition);
  WriteHex64Field("vertex_shader_hash", draw.vertex_shader_hash);
  WriteHex64Field("pixel_shader_hash", draw.pixel_shader_hash);
  EndEvent();
}

void NativeRenderCaptureWriter::WriteShaderRecordProbe(
    const ShaderRecordProbeInfo &probe) {
  std::scoped_lock lock(mutex_);
  if (!BeginEvent("shader_record_probe")) {
    return;
  }
  WriteU64Field("event", probe.event_index);
  WriteStringField("function", probe.function_name);
  WriteHex32Field("function_address", probe.function_address);
  WriteHex64Field("link_register", probe.link_register);
  WriteHex32Field("r3", probe.r3);
  WriteHex32Field("r4", probe.r4);
  WriteHex32Field("r5", probe.r5);
  WriteHex32Field("r6", probe.r6);
  WriteHex32Field("r7", probe.r7);
  WriteHex32Field("r8", probe.r8);
  WriteHex32Field("r9", probe.r9);
  WriteHex32Field("r10", probe.r10);
  WriteHex32Field("r28", probe.r28);
  WriteHex32Field("r29", probe.r29);
  WriteHex32Field("r30", probe.r30);
  WriteHex32Field("r31", probe.r31);
  WriteHex32Field("command_buffer_object", probe.command_buffer_object);
  WriteHex32Field("write_begin", probe.write_begin);
  WriteHex32Field("write_end", probe.write_end);
  WriteHex32Field("write_limit_begin", probe.write_limit_begin);
  WriteHex32Field("write_limit_end", probe.write_limit_end);
  WriteHex32Field("return_value", probe.return_value);
  WriteHex32Field("primary_address", probe.primary_address);
  WriteU64Field("primary_dword_count_hint", probe.primary_dword_count_hint);
  WriteU64Field("primary_dword_count", probe.primary_dword_count);
  WriteBoolField("primary_truncated", probe.primary_truncated);
  WriteBoolField("primary_missing", probe.primary_missing);
  WriteFieldPrefix("primary_dwords");
  file_ << '[';
  for (uint32_t i = 0;
       i < probe.primary_dword_count && i < probe.primary_dwords.size(); ++i) {
    if (i) {
      file_ << ',';
    }
    file_ << '"' << HexValue(probe.primary_dwords[i], 8) << '"';
  }
  file_ << ']';
  WriteHex32Field("secondary_address", probe.secondary_address);
  WriteU64Field("secondary_dword_count", probe.secondary_dword_count);
  WriteBoolField("secondary_truncated", probe.secondary_truncated);
  WriteBoolField("secondary_missing", probe.secondary_missing);
  WriteFieldPrefix("secondary_dwords");
  file_ << '[';
  for (uint32_t i = 0; i < probe.secondary_dword_count &&
                       i < probe.secondary_dwords.size();
       ++i) {
    if (i) {
      file_ << ',';
    }
    file_ << '"' << HexValue(probe.secondary_dwords[i], 8) << '"';
  }
  file_ << ']';
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
  WriteU64Field("payload_dword_count", shader.payload_dword_count);
  WriteBoolField("payload_truncated", shader.payload_truncated);
  WriteBoolField("payload_missing", shader.payload_missing);
  WriteFieldPrefix("dwords");
  file_ << '[';
  for (uint32_t i = 0;
       i < shader.payload_dword_count && i < shader.payload_dwords.size();
       ++i) {
    if (i) {
      file_ << ',';
    }
    file_ << '"' << HexValue(shader.payload_dwords[i], 8) << '"';
  }
  file_ << ']';
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
