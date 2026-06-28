#include "NativeRenderReplay.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

namespace bo2::native::replay {

namespace {

enum class JsonValueType {
  Null,
  String,
  Number,
  Bool,
  Array,
};

struct JsonValue {
  JsonValueType type = JsonValueType::Null;
  std::string string_value;
  uint64_t number_value = 0;
  bool bool_value = false;
  std::vector<JsonValue> array_value;
};

using JsonObject = std::unordered_map<std::string, JsonValue>;

class JsonLineParser {
public:
  explicit JsonLineParser(std::string_view text) : text_(text) {}

  bool ParseObject(JsonObject &out, std::string &error) {
    SkipWhitespace();
    if (!Consume('{')) {
      error = "expected object";
      return false;
    }

    SkipWhitespace();
    if (Consume('}')) {
      return true;
    }

    while (pos_ < text_.size()) {
      std::string key;
      if (!ParseString(key, error)) {
        return false;
      }
      SkipWhitespace();
      if (!Consume(':')) {
        error = "expected ':' after key";
        return false;
      }
      JsonValue value;
      if (!ParseValue(value, error)) {
        return false;
      }
      out.emplace(std::move(key), std::move(value));
      SkipWhitespace();
      if (Consume('}')) {
        SkipWhitespace();
        if (pos_ != text_.size()) {
          error = "trailing characters after object";
          return false;
        }
        return true;
      }
      if (!Consume(',')) {
        error = "expected ',' or '}'";
        return false;
      }
      SkipWhitespace();
    }

    error = "unterminated object";
    return false;
  }

private:
  void SkipWhitespace() {
    while (pos_ < text_.size()) {
      const char ch = text_[pos_];
      if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
        break;
      }
      ++pos_;
    }
  }

  bool Consume(char ch) {
    SkipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == ch) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool ParseValue(JsonValue &out, std::string &error) {
    SkipWhitespace();
    if (pos_ >= text_.size()) {
      error = "expected value";
      return false;
    }

    if (text_[pos_] == '"') {
      out.type = JsonValueType::String;
      return ParseString(out.string_value, error);
    }
    if (text_[pos_] == '[') {
      out.type = JsonValueType::Array;
      return ParseArray(out.array_value, error);
    }
    if (StartsWith("true")) {
      pos_ += 4;
      out.type = JsonValueType::Bool;
      out.bool_value = true;
      return true;
    }
    if (StartsWith("false")) {
      pos_ += 5;
      out.type = JsonValueType::Bool;
      out.bool_value = false;
      return true;
    }
    if (StartsWith("null")) {
      pos_ += 4;
      out.type = JsonValueType::Null;
      return true;
    }
    if (text_[pos_] >= '0' && text_[pos_] <= '9') {
      out.type = JsonValueType::Number;
      return ParseNumber(out.number_value, error);
    }

    error = "unsupported value";
    return false;
  }

  bool ParseString(std::string &out, std::string &error) {
    SkipWhitespace();
    if (pos_ >= text_.size() || text_[pos_] != '"') {
      error = "expected string";
      return false;
    }
    ++pos_;
    out.clear();

    while (pos_ < text_.size()) {
      char ch = text_[pos_++];
      if (ch == '"') {
        return true;
      }
      if (ch != '\\') {
        out.push_back(ch);
        continue;
      }
      if (pos_ >= text_.size()) {
        error = "unterminated string escape";
        return false;
      }
      const char escaped = text_[pos_++];
      switch (escaped) {
      case '"':
      case '\\':
      case '/':
        out.push_back(escaped);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'u':
        if (!ParseUnicodeEscape(out, error)) {
          return false;
        }
        break;
      default:
        error = "invalid string escape";
        return false;
      }
    }

    error = "unterminated string";
    return false;
  }

  bool ParseUnicodeEscape(std::string &out, std::string &error) {
    if (pos_ + 4 > text_.size()) {
      error = "short unicode escape";
      return false;
    }

    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = text_[pos_++];
      value <<= 4;
      if (ch >= '0' && ch <= '9') {
        value |= static_cast<uint32_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        value |= static_cast<uint32_t>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        value |= static_cast<uint32_t>(ch - 'A' + 10);
      } else {
        error = "invalid unicode escape";
        return false;
      }
    }

    out.push_back(value <= 0x7F ? static_cast<char>(value) : '?');
    return true;
  }

  bool ParseNumber(uint64_t &out, std::string &error) {
    const std::size_t begin = pos_;
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
      ++pos_;
    }

    const auto number = text_.substr(begin, pos_ - begin);
    const char *first = number.data();
    const char *last = first + number.size();
    const auto result = std::from_chars(first, last, out, 10);
    if (result.ec != std::errc{} || result.ptr != last) {
      error = "invalid number";
      return false;
    }
    return true;
  }

  bool ParseArray(std::vector<JsonValue> &out, std::string &error) {
    if (!Consume('[')) {
      error = "expected array";
      return false;
    }

    SkipWhitespace();
    if (Consume(']')) {
      return true;
    }

    while (pos_ < text_.size()) {
      JsonValue value;
      if (!ParseValue(value, error)) {
        return false;
      }
      out.push_back(std::move(value));
      SkipWhitespace();
      if (Consume(']')) {
        return true;
      }
      if (!Consume(',')) {
        error = "expected ',' or ']'";
        return false;
      }
    }

    error = "unterminated array";
    return false;
  }

  bool StartsWith(std::string_view value) const {
    return text_.substr(pos_, value.size()) == value;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

const JsonValue *FindValue(const JsonObject &object, std::string_view key) {
  const auto it = object.find(std::string(key));
  return it == object.end() ? nullptr : &it->second;
}

bool ParseIntegerText(std::string_view text, uint64_t &out) {
  int base = 10;
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2);
    base = 16;
  }

  if (text.empty()) {
    return false;
  }

  const char *first = text.data();
  const char *last = first + text.size();
  const auto result = std::from_chars(first, last, out, base);
  return result.ec == std::errc{} && result.ptr == last;
}

uint64_t GetU64(const JsonObject &object, std::string_view key,
                uint64_t default_value = 0) {
  const JsonValue *value = FindValue(object, key);
  if (!value) {
    return default_value;
  }
  if (value->type == JsonValueType::Number) {
    return value->number_value;
  }
  if (value->type == JsonValueType::String) {
    uint64_t parsed = 0;
    return ParseIntegerText(value->string_value, parsed) ? parsed
                                                         : default_value;
  }
  return default_value;
}

uint32_t GetU32(const JsonObject &object, std::string_view key,
                uint32_t default_value = 0) {
  const uint64_t value = GetU64(object, key, default_value);
  return value > std::numeric_limits<uint32_t>::max()
             ? default_value
             : static_cast<uint32_t>(value);
}

bool GetBool(const JsonObject &object, std::string_view key,
             bool default_value = false) {
  const JsonValue *value = FindValue(object, key);
  return value && value->type == JsonValueType::Bool ? value->bool_value
                                                     : default_value;
}

std::string GetString(const JsonObject &object, std::string_view key,
                      std::string default_value = {}) {
  const JsonValue *value = FindValue(object, key);
  return value && value->type == JsonValueType::String
             ? value->string_value
             : std::move(default_value);
}

std::vector<uint32_t> GetU32Array(const JsonObject &object,
                                  std::string_view key) {
  std::vector<uint32_t> values;
  const JsonValue *value = FindValue(object, key);
  if (!value || value->type != JsonValueType::Array) {
    return values;
  }

  values.reserve(value->array_value.size());
  for (const JsonValue &element : value->array_value) {
    if (element.type == JsonValueType::Number) {
      if (element.number_value <= std::numeric_limits<uint32_t>::max()) {
        values.push_back(static_cast<uint32_t>(element.number_value));
      }
      continue;
    }
    if (element.type == JsonValueType::String) {
      uint64_t parsed = 0;
      if (ParseIntegerText(element.string_value, parsed) &&
          parsed <= std::numeric_limits<uint32_t>::max()) {
        values.push_back(static_cast<uint32_t>(parsed));
      }
    }
  }
  return values;
}

bool ParseCaptureEvent(const JsonObject &object, uint64_t line,
                       CaptureEvent &event, std::string &error) {
  event.line = line;
  event.seq = GetU64(object, "seq");
  event.type_name = GetString(object, "type");
  event.type = ParseCaptureEventType(event.type_name);
  if (event.seq == 0) {
    error = "missing or zero seq";
    return false;
  }
  if (event.type_name.empty()) {
    error = "missing type";
    return false;
  }

  if (FindValue(object, "frame")) {
    event.frame = GetU64(object, "frame");
  }

  switch (event.type) {
  case CaptureEventType::PM4Packet:
    event.packet.event = GetU64(object, "event");
    event.packet.opcode = GetU32(object, "opcode");
    event.packet.packet = GetU32(object, "packet");
    event.packet.payload_dword_count = GetU32(object, "payload_dword_count");
    event.packet.packet_ptr = GetU32(object, "packet_ptr");
    event.packet.buffer_ptr = GetU32(object, "buffer_ptr");
    event.packet.packet_offset = GetU32(object, "packet_offset");
    event.packet.first_dwords = GetU32Array(object, "first_dwords");
    break;
  case CaptureEventType::PM4Shader:
    event.shader.event = GetU64(object, "event");
    event.shader.opcode = GetU32(object, "opcode");
    event.shader.packet = GetU32(object, "packet");
    event.shader.packet_ptr = GetU32(object, "packet_ptr");
    event.shader.buffer_ptr = GetU32(object, "buffer_ptr");
    event.shader.packet_offset = GetU32(object, "packet_offset");
    event.shader.shader_type = GetU32(object, "shader_type");
    event.shader.embedded = GetBool(object, "embedded");
    event.shader.guest_address = GetU32(object, "guest_address");
    event.shader.host_address = GetU64(object, "host_address");
    event.shader.dword_count = GetU32(object, "dword_count");
    event.shader.shader_hash = GetU64(object, "shader_hash");
    break;
  case CaptureEventType::PM4Constants:
    event.constants.seq = event.seq;
    event.constants.event = GetU64(object, "event");
    event.constants.opcode = GetU32(object, "opcode");
    event.constants.packet = GetU32(object, "packet");
    event.constants.packet_ptr = GetU32(object, "packet_ptr");
    event.constants.buffer_ptr = GetU32(object, "buffer_ptr");
    event.constants.packet_offset = GetU32(object, "packet_offset");
    event.constants.address = GetU32(object, "address");
    event.constants.offset_type = GetU32(object, "offset_type");
    event.constants.constant_type = GetU32(object, "constant_type");
    event.constants.index = GetU32(object, "index");
    event.constants.dword_count = GetU32(object, "dword_count");
    event.constants.payload_dword_count = GetU32(object, "payload_dword_count");
    event.constants.dwords = GetU32Array(object, "dwords");
    if (event.constants.dwords.empty()) {
      event.constants.dwords = GetU32Array(object, "payload_dwords");
    }
    if (event.constants.payload_dword_count == 0 &&
        !event.constants.dwords.empty()) {
      event.constants.payload_dword_count =
          static_cast<uint32_t>(event.constants.dwords.size());
    }
    event.constants.payload_truncated = GetBool(object, "payload_truncated");
    event.constants.payload_missing = GetBool(
        object, "payload_missing",
        !FindValue(object, "dwords") && !FindValue(object, "payload_dwords"));
    if (!event.constants.dwords.empty()) {
      event.constants.payload_missing = false;
    }
    break;
  case CaptureEventType::PM4Draw:
    event.draw.event = GetU64(object, "event");
    event.draw.opcode_name = GetString(object, "opcode_name");
    event.draw.opcode = GetU32(object, "opcode");
    event.draw.packet = GetU32(object, "packet");
    event.draw.packet_ptr = GetU32(object, "packet_ptr");
    event.draw.buffer_ptr = GetU32(object, "buffer_ptr");
    event.draw.packet_offset = GetU32(object, "packet_offset");
    event.draw.index_count = GetU32(object, "index_count");
    event.draw.primitive_type = GetU32(object, "primitive_type");
    event.draw.source_select = GetU32(object, "source_select");
    event.draw.indexed = GetBool(object, "indexed");
    event.draw.index_base = GetU32(object, "index_base");
    event.draw.index_length = GetU32(object, "index_length");
    event.draw.index_buffer_count = GetU32(object, "index_buffer_count");
    event.draw.index_format = GetU32(object, "index_format");
    event.draw.index_endianness = GetU32(object, "index_endianness");
    event.draw.major_mode = GetU32(object, "major_mode");
    event.draw.explicit_major_mode = GetBool(object, "explicit_major_mode");
    event.draw.viz_query_condition = GetU32(object, "viz_query_condition");
    event.draw.vertex_shader_hash = GetU64(object, "vertex_shader_hash");
    event.draw.pixel_shader_hash = GetU64(object, "pixel_shader_hash");
    break;
  case CaptureEventType::PM4Swap:
    event.swap.event = GetU64(object, "event");
    event.swap.opcode = GetU32(object, "opcode");
    event.swap.packet = GetU32(object, "packet");
    event.swap.packet_ptr = GetU32(object, "packet_ptr");
    event.swap.buffer_ptr = GetU32(object, "buffer_ptr");
    event.swap.packet_offset = GetU32(object, "packet_offset");
    event.swap.frontbuffer_ptr = GetU32(object, "frontbuffer_ptr");
    event.swap.width = GetU32(object, "width");
    event.swap.height = GetU32(object, "height");
    event.swap.frame_counter = GetU32(object, "frame_counter");
    break;
  case CaptureEventType::RenderCommand:
    event.command.sequence = GetU64(object, "sequence");
    event.command.command = GetString(object, "command");
    event.command.guest_address = GetU32(object, "guest_address");
    event.command.arg0 = GetU32(object, "arg0");
    event.command.arg1 = GetU32(object, "arg1");
    event.command.arg2 = GetU32(object, "arg2");
    event.command.arg3 = GetU32(object, "arg3");
    break;
  case CaptureEventType::VdSwap:
    event.vd_swap.frame = GetU64(object, "frame");
    event.vd_swap.command_buffer = GetU32(object, "command_buffer");
    event.vd_swap.fetch_constant = GetU32(object, "fetch_constant");
    event.vd_swap.writeback = GetU32(object, "writeback");
    event.vd_swap.system_command_buffer =
        GetU32(object, "system_command_buffer");
    event.vd_swap.system_command_buffer_token =
        GetU32(object, "system_command_buffer_token");
    event.vd_swap.frontbuffer_ptr = GetU32(object, "frontbuffer_ptr");
    event.vd_swap.texture_format_ptr = GetU32(object, "texture_format_ptr");
    event.vd_swap.color_space_ptr = GetU32(object, "color_space_ptr");
    event.vd_swap.width_ptr = GetU32(object, "width_ptr");
    event.vd_swap.height_ptr = GetU32(object, "height_ptr");
    break;
  case CaptureEventType::PresentSnapshot:
    event.present.frame = GetU64(object, "frame");
    event.present.command_buffer = GetU32(object, "command_buffer");
    event.present.dword_count = GetU32(object, "dword_count");
    event.present.has_xe_swap = GetBool(object, "has_xe_swap");
    event.present.xe_swap_dword_offset = GetU32(object, "xe_swap_dword_offset");
    event.present.xe_swap_packet = GetU32(object, "xe_swap_packet");
    event.present.swap_signature = GetU32(object, "swap_signature");
    event.present.frontbuffer_physical = GetU32(object, "frontbuffer_physical");
    event.present.width = GetU32(object, "width");
    event.present.height = GetU32(object, "height");
    event.present.dwords = GetU32Array(object, "dwords");
    break;
  case CaptureEventType::DrawPacketCandidate:
    event.draw_candidate.event = GetU64(object, "event");
    event.draw_candidate.function = GetString(object, "function");
    event.draw_candidate.function_address = GetU32(object, "function_address");
    event.draw_candidate.link_register = GetU64(object, "link_register");
    event.draw_candidate.command_buffer_object =
        GetU32(object, "command_buffer_object");
    event.draw_candidate.write_begin = GetU32(object, "write_begin");
    event.draw_candidate.write_end = GetU32(object, "write_end");
    event.draw_candidate.write_limit = GetU32(object, "write_limit");
    event.draw_candidate.dword_count = GetU32(object, "dword_count");
    event.draw_candidate.truncated = GetBool(object, "truncated");
    event.draw_candidate.has_draw_indx_2 = GetBool(object, "has_draw_indx_2");
    event.draw_candidate.draw_packet_dword_offset =
        GetU32(object, "draw_packet_dword_offset");
    event.draw_candidate.draw_packet = GetU32(object, "draw_packet");
    event.draw_candidate.draw_initiator = GetU32(object, "draw_initiator");
    event.draw_candidate.index_count = GetU32(object, "index_count");
    event.draw_candidate.primitive_type = GetU32(object, "primitive_type");
    event.draw_candidate.source_select = GetU32(object, "source_select");
    event.draw_candidate.index_32bit = GetBool(object, "index_32bit");
    break;
  default:
    break;
  }

  return true;
}

uint64_t RenderCommandShaderHash(const RenderCommandRecord &command) {
  return (static_cast<uint64_t>(command.arg2) << 32) | command.arg1;
}

void AddWarning(ReplayCapture &capture, const ReplayLoadOptions &options,
                std::string message) {
  if (capture.warnings.size() < options.max_warnings) {
    capture.warnings.push_back(std::move(message));
  }
}

std::string FrameLabel(const ReplayDrawState &draw_state) {
  if (!draw_state.frame_index) {
    return "pre";
  }
  std::ostringstream os;
  os << *draw_state.frame_index << "/id=" << *draw_state.frame_id;
  return os.str();
}

void CountCurrentFrameEvent(ReplayCapture &capture,
                            std::optional<std::size_t> current_frame,
                            CaptureEventType type) {
  if (current_frame) {
    ++capture.frames[*current_frame].event_counts[type];
  } else {
    ++capture.summary.pre_frame_events;
  }
}

void AnalyzeReplayCapture(ReplayCapture &capture,
                          const ReplayLoadOptions &options) {
  BoundShaderState current_vertex_shader;
  BoundShaderState current_pixel_shader;
  uint64_t constants_seen_total = 0;
  uint64_t constants_seen_in_frame = 0;
  uint64_t last_constant_seq = 0;
  std::vector<PM4ConstantRecord> recent_constants;
  std::map<std::pair<uint32_t, uint32_t>, PM4ConstantRecord> bound_constants;
  std::optional<std::size_t> current_frame;
  std::map<std::pair<uint32_t, uint64_t>, ShaderUsageRecord> shader_usage;
  std::map<std::pair<uint64_t, uint64_t>, ShaderPairUsageRecord>
      shader_pair_usage;

  auto bind_shader = [&](uint32_t shader_type, uint64_t hash,
                         uint32_t guest_address, uint32_t dword_count,
                         uint64_t seq) {
    if (hash == 0) {
      return;
    }
    BoundShaderState state{};
    state.hash = hash;
    state.guest_address = guest_address;
    state.dword_count = dword_count;
    state.seq = seq;
    if (shader_type == 0) {
      current_vertex_shader = state;
    } else if (shader_type == 1) {
      current_pixel_shader = state;
    }
  };

  for (std::size_t event_index = 0; event_index < capture.events.size();
       ++event_index) {
    const CaptureEvent &event = capture.events[event_index];
    ++capture.summary.total_events;
    ++capture.summary.event_counts[event.type];

    if (event.type == CaptureEventType::BeginFrame) {
      if (current_frame) {
        AddWarning(capture, options,
                   "frame began before previous frame ended at seq " +
                       std::to_string(event.seq));
      }
      ReplayFrame frame{};
      frame.index = capture.frames.size();
      frame.frame_id = event.frame.value_or(0);
      frame.begin_seq = event.seq;
      frame.has_begin = true;
      frame.first_draw_index = capture.draws.size();
      capture.frames.push_back(frame);
      current_frame = capture.frames.back().index;
      constants_seen_in_frame = 0;
      ++capture.frames[*current_frame].event_counts[event.type];
      continue;
    }

    if (event.type == CaptureEventType::EndFrame) {
      CountCurrentFrameEvent(capture, current_frame, event.type);
      if (!current_frame) {
        AddWarning(capture, options,
                   "end_frame without open frame at seq " +
                       std::to_string(event.seq));
        continue;
      }
      ReplayFrame &frame = capture.frames[*current_frame];
      frame.has_end = true;
      frame.end_seq = event.seq;
      current_frame.reset();
      continue;
    }

    CountCurrentFrameEvent(capture, current_frame, event.type);

    switch (event.type) {
    case CaptureEventType::PM4Shader: {
      auto key =
          std::make_pair(event.shader.shader_type, event.shader.shader_hash);
      ShaderUsageRecord &usage = shader_usage[key];
      usage.hash = event.shader.shader_hash;
      usage.shader_type = event.shader.shader_type;
      ++usage.loads;
      usage.max_dwords = std::max(usage.max_dwords, event.shader.dword_count);
      bind_shader(event.shader.shader_type, event.shader.shader_hash,
                  event.shader.guest_address, event.shader.dword_count,
                  event.seq);
      if (current_frame) {
        ++capture.frames[*current_frame].shader_count;
      }
      break;
    }
    case CaptureEventType::RenderCommand:
      if (event.command.command == "BindShader") {
        bind_shader(event.command.arg0, RenderCommandShaderHash(event.command),
                    event.command.guest_address, event.command.arg3, event.seq);
      }
      break;
    case CaptureEventType::PM4Constants:
      ++constants_seen_total;
      ++constants_seen_in_frame;
      last_constant_seq = event.seq;
      recent_constants.push_back(event.constants);
      bound_constants[{event.constants.constant_type, event.constants.index}] =
          event.constants;
      if (event.constants.payload_missing) {
        ++capture.summary.constant_uploads_missing_payload;
      } else {
        ++capture.summary.constant_uploads_with_payload;
        capture.summary.constant_payload_dwords +=
            event.constants.dwords.size();
      }
      if (event.constants.payload_truncated) {
        ++capture.summary.constant_payload_truncated;
      }
      if (recent_constants.size() > 8) {
        recent_constants.erase(recent_constants.begin());
      }
      if (current_frame) {
        ++capture.frames[*current_frame].constant_count;
      }
      break;
    case CaptureEventType::PM4Draw: {
      ReplayDrawState draw_state{};
      draw_state.draw_index = capture.draws.size();
      draw_state.event_index = event_index;
      draw_state.seq = event.seq;
      draw_state.frame_index = current_frame;
      if (current_frame) {
        draw_state.frame_id = capture.frames[*current_frame].frame_id;
      }
      draw_state.draw = event.draw;
      draw_state.vertex_shader = current_vertex_shader;
      draw_state.pixel_shader = current_pixel_shader;
      if (event.draw.vertex_shader_hash != 0) {
        draw_state.vertex_shader.hash = event.draw.vertex_shader_hash;
        draw_state.vertex_shader.seq = event.seq;
      }
      if (event.draw.pixel_shader_hash != 0) {
        draw_state.pixel_shader.hash = event.draw.pixel_shader_hash;
        draw_state.pixel_shader.seq = event.seq;
      }
      draw_state.constants_seen_total = constants_seen_total;
      draw_state.constants_seen_in_frame = constants_seen_in_frame;
      draw_state.last_constant_seq = last_constant_seq;
      draw_state.recent_constants = recent_constants;
      draw_state.bound_constants.reserve(bound_constants.size());
      for (const auto &[key, constant] : bound_constants) {
        (void)key;
        draw_state.bound_constants.push_back(constant);
      }
      draw_state.missing_vertex_shader = draw_state.vertex_shader.hash == 0;
      draw_state.missing_pixel_shader = draw_state.pixel_shader.hash == 0;
      draw_state.missing_constants = constants_seen_total == 0;

      if (draw_state.missing_vertex_shader) {
        ++capture.summary.missing_vertex_shader_draws;
      }
      if (draw_state.missing_pixel_shader) {
        ++capture.summary.missing_pixel_shader_draws;
      }
      if (draw_state.missing_constants) {
        ++capture.summary.missing_constant_draws;
      }

      ++capture.summary.draw_opcode_counts[event.draw.opcode];
      ++capture.summary.primitive_counts[event.draw.primitive_type];
      ++capture.summary.source_select_counts[event.draw.source_select];

      auto vs_key = std::make_pair(uint32_t{0}, draw_state.vertex_shader.hash);
      auto ps_key = std::make_pair(uint32_t{1}, draw_state.pixel_shader.hash);
      shader_usage[vs_key].hash = draw_state.vertex_shader.hash;
      shader_usage[vs_key].shader_type = 0;
      ++shader_usage[vs_key].draws;
      shader_usage[ps_key].hash = draw_state.pixel_shader.hash;
      shader_usage[ps_key].shader_type = 1;
      ++shader_usage[ps_key].draws;

      auto pair_key = std::make_pair(draw_state.vertex_shader.hash,
                                     draw_state.pixel_shader.hash);
      ShaderPairUsageRecord &pair_usage = shader_pair_usage[pair_key];
      pair_usage.vertex_hash = draw_state.vertex_shader.hash;
      pair_usage.pixel_hash = draw_state.pixel_shader.hash;
      ++pair_usage.draws;

      if (current_frame) {
        ReplayFrame &frame = capture.frames[*current_frame];
        if (frame.draw_count == 0) {
          frame.first_draw_index = capture.draws.size();
        }
        ++frame.draw_count;
      }
      capture.draws.push_back(std::move(draw_state));
      break;
    }
    case CaptureEventType::PM4Swap:
    case CaptureEventType::VdSwap:
    case CaptureEventType::PresentSnapshot:
      if (current_frame) {
        ++capture.frames[*current_frame].swap_count;
      }
      break;
    default:
      break;
    }
  }

  if (current_frame) {
    AddWarning(capture, options,
               "capture ended with open frame index " +
                   std::to_string(*current_frame));
  }

  capture.shader_usage.reserve(shader_usage.size());
  for (const auto &[key, value] : shader_usage) {
    if (value.hash != 0) {
      capture.shader_usage.push_back(value);
    }
  }
  std::sort(capture.shader_usage.begin(), capture.shader_usage.end(),
            [](const ShaderUsageRecord &lhs, const ShaderUsageRecord &rhs) {
              if (lhs.draws != rhs.draws) {
                return lhs.draws > rhs.draws;
              }
              if (lhs.loads != rhs.loads) {
                return lhs.loads > rhs.loads;
              }
              return lhs.hash < rhs.hash;
            });

  capture.shader_pair_usage.reserve(shader_pair_usage.size());
  for (const auto &[key, value] : shader_pair_usage) {
    if (value.vertex_hash != 0 || value.pixel_hash != 0) {
      capture.shader_pair_usage.push_back(value);
    }
  }
  std::sort(
      capture.shader_pair_usage.begin(), capture.shader_pair_usage.end(),
      [](const ShaderPairUsageRecord &lhs, const ShaderPairUsageRecord &rhs) {
        if (lhs.draws != rhs.draws) {
          return lhs.draws > rhs.draws;
        }
        if (lhs.vertex_hash != rhs.vertex_hash) {
          return lhs.vertex_hash < rhs.vertex_hash;
        }
        return lhs.pixel_hash < rhs.pixel_hash;
      });
}

std::string FormatHex(uint64_t value, int width) {
  std::ostringstream os;
  os << "0x" << std::uppercase << std::hex << std::setfill('0')
     << std::setw(width) << value;
  return os.str();
}

const char *ShaderTypeName(uint32_t shader_type) {
  switch (shader_type) {
  case 0:
    return "VS";
  case 1:
    return "PS";
  default:
    return "?";
  }
}

std::string DrawStateFlags(const ReplayDrawState &draw_state) {
  std::string flags;
  if (draw_state.missing_vertex_shader) {
    flags += " missing_vs";
  }
  if (draw_state.missing_pixel_shader) {
    flags += " missing_ps";
  }
  if (draw_state.missing_constants) {
    flags += " no_constants_yet";
  }
  return flags.empty() ? " ok" : flags;
}

std::string ConstantPayloadStatus(const PM4ConstantRecord &constant) {
  std::ostringstream os;
  if (constant.payload_missing) {
    os << "payload=missing";
    return os.str();
  }
  os << "payload=" << constant.dwords.size() << "/" << constant.dword_count
     << " dwords";
  if (constant.payload_truncated) {
    os << " truncated";
  }
  return os.str();
}

void PrintDwordPreview(const std::vector<uint32_t> &dwords,
                       std::size_t max_dwords = 8) {
  if (dwords.empty()) {
    return;
  }
  std::cout << " values=";
  const std::size_t limit = std::min(dwords.size(), max_dwords);
  for (std::size_t i = 0; i < limit; ++i) {
    if (i) {
      std::cout << ",";
    }
    std::cout << FormatHex32(dwords[i]);
  }
  if (dwords.size() > limit) {
    std::cout << ",...";
  }
}

void PrintConstantLine(const PM4ConstantRecord &constant,
                       std::string_view indent) {
  std::cout << indent << "seq=" << constant.seq << " event=" << constant.event
            << " opcode=" << FormatHex32(constant.opcode)
            << " packet=" << FormatHex32(constant.packet)
            << " packet_ptr=" << FormatHex32(constant.packet_ptr)
            << " addr=" << FormatHex32(constant.address)
            << " offset_type=" << FormatHex32(constant.offset_type)
            << " type=" << constant.constant_type << " index=" << constant.index
            << " dwords=" << constant.dword_count << " "
            << ConstantPayloadStatus(constant);
  PrintDwordPreview(constant.dwords);
  std::cout << "\n";
}

bool IsEventInsideFrame(const ReplayFrame &frame, uint64_t seq) {
  if (!frame.has_begin) {
    return false;
  }
  if (frame.has_end) {
    return seq >= frame.begin_seq && seq <= frame.end_seq;
  }
  return seq >= frame.begin_seq;
}

void PrintHistogram(const char *title,
                    const std::map<uint32_t, uint64_t> &values,
                    bool hex_key = false) {
  std::cout << title << ":\n";
  if (values.empty()) {
    std::cout << "  (none)\n";
    return;
  }
  for (const auto &[key, count] : values) {
    std::cout << "  " << (hex_key ? FormatHex32(key) : std::to_string(key))
              << ": " << count << "\n";
  }
}

bool ParseSizeArgument(std::string_view text, std::size_t &out) {
  uint64_t parsed = 0;
  if (!ParseIntegerText(text, parsed) ||
      parsed > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  out = static_cast<std::size_t>(parsed);
  return true;
}

enum class ReplayBackendKind {
  Null,
  D3D12Diagnostic,
  D3D12Real,
  VulkanDiagnostic,
  VulkanReal,
  Unknown,
};

ReplayBackendKind ParseBackendKind(const std::string &backend) {
  if (backend == "null" || backend == "offline") {
    return ReplayBackendKind::Null;
  }
  if (backend == "d3d12-diagnostic" || backend == "d3d12-debug") {
    return ReplayBackendKind::D3D12Diagnostic;
  }
  if (backend == "d3d12" || backend == "d3d12-real") {
    return ReplayBackendKind::D3D12Real;
  }
  if (backend == "vulkan-diagnostic" || backend == "vulkan-debug") {
    return ReplayBackendKind::VulkanDiagnostic;
  }
  if (backend == "vulkan") {
    return ReplayBackendKind::VulkanReal;
  }
  return ReplayBackendKind::Unknown;
}

void PrintHelp() {
  std::cout
      << "native_render_replay --capture <capture.jsonl> [options]\n\n"
      << "Options:\n"
      << "  --summary              Print capture/frame/draw summary (default)\n"
      << "  --no-summary           Suppress summary\n"
      << "  --frame <index>        Select zero-based replay frame for "
         "frame/draw output\n"
      << "  --dump-draws           Dump reconstructed draw state\n"
      << "  --draw <index>         Dump one zero-based global draw\n"
      << "  --dump-bound-state     Dump full known state for --draw\n"
      << "  --dump-constants       Dump constant uploads, optionally filtered "
         "by "
         "--frame or --draw\n"
      << "  --dump-indices         Dump decoded index data for --draw\n"
      << "  --dump-vertices        Dump decoded vertex/fetch data for --draw\n"
      << "  --resource-summary     Summarize replay resource snapshot "
         "coverage\n"
      << "  --max-draws <count>    Limit draw dump rows (default 64)\n"
      << "  --shader-usage         Print shader and shader-pair usage\n"
      << "  --top-shaders <count>  Limit shader usage rows (default 20)\n"
      << "  --missing-shaders      Report draws missing runtime shader hashes\n"
      << "  --backend <name>       Replay backend selector: "
         "null/d3d12-diagnostic/d3d12/vulkan-diagnostic/vulkan\n"
      << "  --d3d12-output <path>  BMP output for --backend d3d12-diagnostic\n"
      << "  --d3d12-draws <count>  Replay draw tiles to render (default 4096)\n"
      << "  --validate             Parse/analyze only; output errors decide "
         "exit code\n"
      << "  --help                 Show this help\n";
}

} // namespace

const char *ToString(CaptureEventType type) {
  switch (type) {
  case CaptureEventType::CaptureStart:
    return "capture_start";
  case CaptureEventType::BeginFrame:
    return "begin_frame";
  case CaptureEventType::EndFrame:
    return "end_frame";
  case CaptureEventType::VdSwap:
    return "vd_swap";
  case CaptureEventType::PresentSnapshot:
    return "present_snapshot";
  case CaptureEventType::DrawPacketCandidate:
    return "draw_packet_candidate";
  case CaptureEventType::PM4Packet:
    return "pm4_packet";
  case CaptureEventType::PM4Draw:
    return "pm4_draw";
  case CaptureEventType::PM4Shader:
    return "pm4_shader";
  case CaptureEventType::PM4Constants:
    return "pm4_constants";
  case CaptureEventType::PM4Swap:
    return "pm4_swap";
  case CaptureEventType::RenderCommand:
    return "render_command";
  case CaptureEventType::Unknown:
    return "unknown";
  }
  return "unknown";
}

CaptureEventType ParseCaptureEventType(std::string_view type) {
  if (type == "capture_start") {
    return CaptureEventType::CaptureStart;
  }
  if (type == "begin_frame") {
    return CaptureEventType::BeginFrame;
  }
  if (type == "end_frame") {
    return CaptureEventType::EndFrame;
  }
  if (type == "vd_swap") {
    return CaptureEventType::VdSwap;
  }
  if (type == "present_snapshot") {
    return CaptureEventType::PresentSnapshot;
  }
  if (type == "draw_packet_candidate") {
    return CaptureEventType::DrawPacketCandidate;
  }
  if (type == "pm4_packet") {
    return CaptureEventType::PM4Packet;
  }
  if (type == "pm4_draw") {
    return CaptureEventType::PM4Draw;
  }
  if (type == "pm4_shader") {
    return CaptureEventType::PM4Shader;
  }
  if (type == "pm4_constants") {
    return CaptureEventType::PM4Constants;
  }
  if (type == "pm4_swap") {
    return CaptureEventType::PM4Swap;
  }
  if (type == "render_command") {
    return CaptureEventType::RenderCommand;
  }
  return CaptureEventType::Unknown;
}

bool LoadReplayCapture(const std::filesystem::path &path,
                       const ReplayLoadOptions &options,
                       ReplayCapture &capture) {
  capture = ReplayCapture{};
  capture.path = std::filesystem::absolute(path);

  std::ifstream file(capture.path);
  if (!file) {
    capture.errors.push_back("could not open capture: " +
                             capture.path.string());
    return false;
  }

  std::string line;
  uint64_t line_number = 0;
  while (std::getline(file, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }

    JsonObject object;
    std::string error;
    JsonLineParser parser(line);
    if (!parser.ParseObject(object, error)) {
      ++capture.summary.parse_errors;
      capture.errors.push_back("line " + std::to_string(line_number) + ": " +
                               error);
      continue;
    }

    CaptureEvent event;
    if (!ParseCaptureEvent(object, line_number, event, error)) {
      ++capture.summary.parse_errors;
      capture.errors.push_back("line " + std::to_string(line_number) + ": " +
                               error);
      continue;
    }

    capture.events.push_back(std::move(event));
  }

  AnalyzeReplayCapture(capture, options);
  if (!options.keep_events) {
    capture.events.clear();
  }
  return capture.summary.parse_errors == 0;
}

std::string FormatHex32(uint32_t value) { return FormatHex(value, 8); }

std::string FormatHex64(uint64_t value) { return FormatHex(value, 16); }

void PrintReplaySummary(const ReplayCapture &capture) {
  std::cout << "Capture: " << capture.path.string() << "\n";
  std::cout << "Events: " << capture.summary.total_events
            << " parse_errors=" << capture.summary.parse_errors << "\n";
  std::cout << "Frames: " << capture.frames.size()
            << " pre_frame_events=" << capture.summary.pre_frame_events << "\n";
  std::cout << "Draws: " << capture.draws.size()
            << " missing_vs=" << capture.summary.missing_vertex_shader_draws
            << " missing_ps=" << capture.summary.missing_pixel_shader_draws
            << " no_constants_yet=" << capture.summary.missing_constant_draws
            << "\n";
  std::cout << "Shaders: unique=" << capture.shader_usage.size()
            << " shader_pairs=" << capture.shader_pair_usage.size() << "\n";
  std::cout << "Constants: with_payload="
            << capture.summary.constant_uploads_with_payload
            << " missing_payload="
            << capture.summary.constant_uploads_missing_payload
            << " payload_dwords=" << capture.summary.constant_payload_dwords
            << " truncated=" << capture.summary.constant_payload_truncated
            << "\n";

  std::cout << "\nEvent counts:\n";
  for (const auto &[type, count] : capture.summary.event_counts) {
    std::cout << "  " << ToString(type) << ": " << count << "\n";
  }

  std::cout << "\nFrame overview:\n";
  const std::size_t frame_limit =
      std::min<std::size_t>(capture.frames.size(), 24);
  for (std::size_t i = 0; i < frame_limit; ++i) {
    const ReplayFrame &frame = capture.frames[i];
    std::cout << "  frame[" << i << "] id=" << frame.frame_id
              << " seq=" << frame.begin_seq << "-" << frame.end_seq
              << " draws=" << frame.draw_count
              << " shaders=" << frame.shader_count
              << " constants=" << frame.constant_count
              << " swaps=" << frame.swap_count
              << " closed=" << (frame.has_end ? "yes" : "no") << "\n";
  }
  if (capture.frames.size() > frame_limit) {
    std::cout << "  ... " << (capture.frames.size() - frame_limit)
              << " more frames\n";
  }

  std::cout << "\n";
  PrintHistogram("Draw opcodes", capture.summary.draw_opcode_counts, true);
  PrintHistogram("Primitive types", capture.summary.primitive_counts);
  PrintHistogram("Source selects", capture.summary.source_select_counts);

  if (!capture.warnings.empty()) {
    std::cout << "\nWarnings:\n";
    for (const std::string &warning : capture.warnings) {
      std::cout << "  " << warning << "\n";
    }
  }
  if (!capture.errors.empty()) {
    std::cout << "\nErrors:\n";
    for (const std::string &error : capture.errors) {
      std::cout << "  " << error << "\n";
    }
  }
}

void PrintFrameSummary(const ReplayCapture &capture, std::size_t frame_index) {
  if (frame_index >= capture.frames.size()) {
    std::cout << "Frame " << frame_index << " does not exist; capture has "
              << capture.frames.size() << " frames\n";
    return;
  }

  const ReplayFrame &frame = capture.frames[frame_index];
  std::cout << "Frame[" << frame.index << "] id=" << frame.frame_id
            << " begin_seq=" << frame.begin_seq << " end_seq=" << frame.end_seq
            << " closed=" << (frame.has_end ? "yes" : "no") << "\n";
  std::cout << "  draws=" << frame.draw_count
            << " shaders=" << frame.shader_count
            << " constants=" << frame.constant_count
            << " swaps=" << frame.swap_count << "\n";
  std::cout << "  events:\n";
  for (const auto &[type, count] : frame.event_counts) {
    std::cout << "    " << ToString(type) << ": " << count << "\n";
  }
}

void PrintDrawDump(const ReplayCapture &capture,
                   std::optional<std::size_t> frame_index,
                   std::optional<std::size_t> draw_index,
                   std::size_t max_draws) {
  std::size_t begin = 0;
  std::size_t end = capture.draws.size();

  if (draw_index) {
    if (*draw_index >= capture.draws.size()) {
      std::cout << "Draw " << *draw_index << " does not exist; capture has "
                << capture.draws.size() << " draws\n";
      return;
    }
    begin = *draw_index;
    end = begin + 1;
  } else if (frame_index) {
    if (*frame_index >= capture.frames.size()) {
      std::cout << "Frame " << *frame_index << " does not exist; capture has "
                << capture.frames.size() << " frames\n";
      return;
    }
    const ReplayFrame &frame = capture.frames[*frame_index];
    begin = frame.first_draw_index;
    end = begin + frame.draw_count;
  }

  const std::size_t unbounded_end = end;
  if (begin >= end) {
    std::cout << "Draw dump: no draws in selected range\n";
    return;
  }

  end = std::min(end, begin + max_draws);
  std::cout << "Draw dump rows " << begin << ".." << (end - 1) << "\n";
  for (std::size_t i = begin; i < end; ++i) {
    const ReplayDrawState &state = capture.draws[i];
    const PM4DrawRecord &draw = state.draw;
    std::cout << "  draw[" << state.draw_index << "] seq=" << state.seq
              << " frame=" << FrameLabel(state) << " "
              << (draw.opcode_name.empty() ? "PM4_DRAW" : draw.opcode_name)
              << " opcode=" << FormatHex32(draw.opcode)
              << " packet=" << FormatHex32(draw.packet)
              << " packet_ptr=" << FormatHex32(draw.packet_ptr)
              << " indices=" << draw.index_count
              << " prim=" << draw.primitive_type
              << " src=" << draw.source_select
              << " indexed=" << (draw.indexed ? "yes" : "no")
              << " index_base=" << FormatHex32(draw.index_base)
              << " index_len=" << draw.index_length
              << " index_fmt=" << draw.index_format
              << " endian=" << draw.index_endianness << "\n";
    std::cout << "    VS=" << FormatHex64(state.vertex_shader.hash)
              << " PS=" << FormatHex64(state.pixel_shader.hash)
              << " constants_total=" << state.constants_seen_total
              << " constants_frame=" << state.constants_seen_in_frame
              << " last_constant_seq=" << state.last_constant_seq
              << " state=" << DrawStateFlags(state) << "\n";
    if (!state.recent_constants.empty()) {
      std::cout << "    recent_constants:";
      for (const PM4ConstantRecord &constant : state.recent_constants) {
        std::cout << " [type=" << constant.constant_type
                  << " index=" << constant.index
                  << " dwords=" << constant.dword_count
                  << " addr=" << FormatHex32(constant.address) << " "
                  << ConstantPayloadStatus(constant) << "]";
      }
      std::cout << "\n";
    }
  }

  if (!draw_index && end < unbounded_end) {
    std::cout << "  ... draw dump limited by --max-draws\n";
  }
}

void PrintConstantsDump(const ReplayCapture &capture,
                        std::optional<std::size_t> frame_index,
                        std::optional<std::size_t> draw_index) {
  if (draw_index) {
    if (*draw_index >= capture.draws.size()) {
      std::cout << "Draw " << *draw_index << " does not exist; capture has "
                << capture.draws.size() << " draws\n";
      return;
    }
    const ReplayDrawState &draw = capture.draws[*draw_index];
    std::cout << "Constant dump for draw[" << draw.draw_index
              << "] seq=" << draw.seq << " frame=" << FrameLabel(draw) << "\n";
    if (draw.bound_constants.empty()) {
      std::cout << "  no constant ranges captured before this draw\n";
      return;
    }
    uint64_t with_payload = 0;
    uint64_t missing_payload = 0;
    for (const PM4ConstantRecord &constant : draw.bound_constants) {
      if (constant.payload_missing) {
        ++missing_payload;
      } else {
        ++with_payload;
      }
      PrintConstantLine(constant, "  ");
    }
    std::cout << "Bound constants=" << draw.bound_constants.size()
              << " with_payload=" << with_payload
              << " missing_payload=" << missing_payload << "\n";
    return;
  }

  const ReplayFrame *selected_frame = nullptr;
  if (frame_index) {
    if (*frame_index >= capture.frames.size()) {
      std::cout << "Frame " << *frame_index << " does not exist; capture has "
                << capture.frames.size() << " frames\n";
      return;
    }
    selected_frame = &capture.frames[*frame_index];
    std::cout << "Constant dump for frame[" << selected_frame->index
              << "] id=" << selected_frame->frame_id << "\n";
  } else {
    std::cout << "Constant dump for full capture\n";
  }

  uint64_t count = 0;
  uint64_t with_payload = 0;
  uint64_t missing_payload = 0;
  for (const CaptureEvent &event : capture.events) {
    if (event.type != CaptureEventType::PM4Constants) {
      continue;
    }
    if (selected_frame && !IsEventInsideFrame(*selected_frame, event.seq)) {
      continue;
    }
    const PM4ConstantRecord &constant = event.constants;
    ++count;
    if (constant.payload_missing) {
      ++missing_payload;
    } else {
      ++with_payload;
    }
    PrintConstantLine(constant, "  ");
  }

  std::cout << "Constants listed=" << count << " with_payload=" << with_payload
            << " missing_payload=" << missing_payload << "\n";
  if (count == 0 && selected_frame) {
    std::cout << "No constants were emitted inside this frame range. "
                 "The current verified capture has most PM4 traffic before "
                 "present-derived frame markers.\n";
  }
}

void PrintBoundStateDump(const ReplayCapture &capture, std::size_t draw_index) {
  if (draw_index >= capture.draws.size()) {
    std::cout << "Draw " << draw_index << " does not exist; capture has "
              << capture.draws.size() << " draws\n";
    return;
  }

  const ReplayDrawState &state = capture.draws[draw_index];
  const PM4DrawRecord &draw = state.draw;
  std::cout << "Bound state for draw[" << state.draw_index
            << "] seq=" << state.seq << " frame=" << FrameLabel(state) << "\n";
  std::cout << "  draw opcode=" << FormatHex32(draw.opcode)
            << " packet=" << FormatHex32(draw.packet)
            << " packet_ptr=" << FormatHex32(draw.packet_ptr)
            << " indices=" << draw.index_count
            << " indexed=" << (draw.indexed ? "yes" : "no")
            << " index_base=" << FormatHex32(draw.index_base)
            << " index_len=" << draw.index_length
            << " index_fmt=" << draw.index_format
            << " endian=" << draw.index_endianness
            << " prim=" << draw.primitive_type << " src=" << draw.source_select
            << "\n";
  std::cout << "  VS hash=" << FormatHex64(state.vertex_shader.hash)
            << " guest=" << FormatHex32(state.vertex_shader.guest_address)
            << " dwords=" << state.vertex_shader.dword_count
            << " bind_seq=" << state.vertex_shader.seq << "\n";
  std::cout << "  PS hash=" << FormatHex64(state.pixel_shader.hash)
            << " guest=" << FormatHex32(state.pixel_shader.guest_address)
            << " dwords=" << state.pixel_shader.dword_count
            << " bind_seq=" << state.pixel_shader.seq << "\n";
  std::cout << "  constants_total=" << state.constants_seen_total
            << " constants_frame=" << state.constants_seen_in_frame
            << " last_constant_seq=" << state.last_constant_seq
            << " bound_ranges=" << state.bound_constants.size()
            << " state=" << DrawStateFlags(state) << "\n";

  if (state.bound_constants.empty()) {
    std::cout << "  bound_constants: none captured before this draw\n";
    return;
  }

  std::cout << "  bound_constants:\n";
  for (const PM4ConstantRecord &constant : state.bound_constants) {
    PrintConstantLine(constant, "    ");
  }
}

void PrintIndexDump(const ReplayCapture &capture, std::size_t draw_index) {
  if (draw_index >= capture.draws.size()) {
    std::cout << "Draw " << draw_index << " does not exist; capture has "
              << capture.draws.size() << " draws\n";
    return;
  }

  const ReplayDrawState &state = capture.draws[draw_index];
  const PM4DrawRecord &draw = state.draw;
  std::cout << "Index dump for draw[" << draw_index << "] seq=" << state.seq
            << "\n";
  std::cout << "  indexed=" << (draw.indexed ? "yes" : "no")
            << " source_select=" << draw.source_select
            << " index_count=" << draw.index_count
            << " index_base=" << FormatHex32(draw.index_base)
            << " index_len=" << draw.index_length
            << " index_buffer_count=" << draw.index_buffer_count
            << " index_format=" << draw.index_format
            << " endian=" << draw.index_endianness << "\n";
  if (!draw.indexed) {
    std::cout << "  no explicit index buffer: this draw is auto-indexed or "
                 "source-selected by the GPU packet\n";
    return;
  }
  std::cout
      << "  raw index snapshot: missing. Current capture stores packet "
         "metadata only; resource snapshots must be added at the CP/guest "
         "memory boundary before decoded indices can be replayed.\n";
}

void PrintVertexDump(const ReplayCapture &capture, std::size_t draw_index) {
  if (draw_index >= capture.draws.size()) {
    std::cout << "Draw " << draw_index << " does not exist; capture has "
              << capture.draws.size() << " draws\n";
    return;
  }

  const ReplayDrawState &state = capture.draws[draw_index];
  std::cout << "Vertex/fetch dump for draw[" << draw_index
            << "] seq=" << state.seq << "\n";
  std::cout << "  VS=" << FormatHex64(state.vertex_shader.hash)
            << " PS=" << FormatHex64(state.pixel_shader.hash)
            << " bound_constant_ranges=" << state.bound_constants.size()
            << "\n";

  uint64_t fetch_like_ranges = 0;
  for (const PM4ConstantRecord &constant : state.bound_constants) {
    if (constant.constant_type == 1) {
      ++fetch_like_ranges;
      PrintConstantLine(constant, "  fetch_candidate ");
    }
  }
  if (fetch_like_ranges == 0) {
    std::cout << "  fetch constants: none identified in current normalized "
                 "state\n";
  }
  std::cout << "  raw vertex snapshot: missing. Vertex buffer addresses, "
               "stride, attribute format, and byte snapshots are not captured "
               "yet, so no decoded vertices can be emitted.\n";
}

void PrintResourceSummary(const ReplayCapture &capture) {
  const auto constant_count_it =
      capture.summary.event_counts.find(CaptureEventType::PM4Constants);
  const uint64_t constant_uploads =
      constant_count_it == capture.summary.event_counts.end()
          ? 0
          : constant_count_it->second;
  std::cout << "Resource snapshot summary:\n";
  std::cout << "  constant_uploads=" << constant_uploads
            << " with_payload=" << capture.summary.constant_uploads_with_payload
            << " missing_payload="
            << capture.summary.constant_uploads_missing_payload
            << " payload_dwords=" << capture.summary.constant_payload_dwords
            << "\n";
  std::cout << "  index_buffer_snapshots=0\n";
  std::cout << "  vertex_buffer_snapshots=0\n";
  std::cout << "  texture_snapshots=0\n";
  std::cout << "  render_target_snapshots=0\n";
  std::cout << "  sidecar_resource_manifest=missing\n";
  std::cout
      << "  real backend blocker: replay has draw/shader/constant metadata, "
         "but no index, vertex, texture, or render-target resource "
         "snapshots yet.\n";
}
void PrintMissingShaders(const ReplayCapture &capture) {
  std::cout << "Missing shader state:\n";
  uint64_t rows = 0;
  for (const ReplayDrawState &draw : capture.draws) {
    if (!draw.missing_vertex_shader && !draw.missing_pixel_shader) {
      continue;
    }
    ++rows;
    std::cout << "  draw[" << draw.draw_index << "] seq=" << draw.seq
              << " frame=" << FrameLabel(draw)
              << " missing_vs=" << (draw.missing_vertex_shader ? "yes" : "no")
              << " missing_ps=" << (draw.missing_pixel_shader ? "yes" : "no")
              << "\n";
  }
  if (rows == 0) {
    std::cout << "  no draws are missing runtime VS/PS hashes\n";
  }
  std::cout << "  replacement shader registry is not bound to replay yet; "
               "use native_shader_inspect for extracted-container lookups.\n";
}

void PrintShaderUsage(const ReplayCapture &capture, std::size_t top_count) {
  std::cout << "Shader usage:\n";
  const std::size_t shader_limit =
      std::min<std::size_t>(capture.shader_usage.size(), top_count);
  for (std::size_t i = 0; i < shader_limit; ++i) {
    const ShaderUsageRecord &usage = capture.shader_usage[i];
    std::cout << "  " << ShaderTypeName(usage.shader_type) << " "
              << FormatHex64(usage.hash) << " draws=" << usage.draws
              << " loads=" << usage.loads << " max_dwords=" << usage.max_dwords
              << "\n";
  }

  std::cout << "\nShader pair usage:\n";
  const std::size_t pair_limit =
      std::min<std::size_t>(capture.shader_pair_usage.size(), top_count);
  for (std::size_t i = 0; i < pair_limit; ++i) {
    const ShaderPairUsageRecord &usage = capture.shader_pair_usage[i];
    std::cout << "  VS=" << FormatHex64(usage.vertex_hash)
              << " PS=" << FormatHex64(usage.pixel_hash)
              << " draws=" << usage.draws << "\n";
  }
}

int RunNativeRenderReplayTool(int argc, char **argv) {
  ReplayCliOptions cli;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    auto require_value = [&](const char *option) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << option << " requires a value\n";
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--help" || arg == "-h") {
      PrintHelp();
      return 0;
    }
    if (arg == "--capture") {
      const char *value = require_value("--capture");
      if (!value) {
        return 2;
      }
      cli.capture_path = value;
    } else if (arg == "--summary") {
      cli.show_summary = true;
    } else if (arg == "--no-summary") {
      cli.show_summary = false;
    } else if (arg == "--dump-draws") {
      cli.dump_draws = true;
    } else if (arg == "--dump-constants") {
      cli.dump_constants = true;
    } else if (arg == "--dump-bound-state") {
      cli.dump_bound_state = true;
    } else if (arg == "--dump-indices") {
      cli.dump_indices = true;
    } else if (arg == "--dump-vertices") {
      cli.dump_vertices = true;
    } else if (arg == "--resource-summary") {
      cli.show_resource_summary = true;
    } else if (arg == "--shader-usage") {
      cli.show_shader_usage = true;
    } else if (arg == "--missing-shaders") {
      cli.show_missing_shaders = true;
    } else if (arg == "--validate") {
      cli.validate_only = true;
    } else if (arg == "--frame") {
      const char *value = require_value("--frame");
      std::size_t parsed = 0;
      if (!value || !ParseSizeArgument(value, parsed)) {
        std::cerr << "--frame expects an integer\n";
        return 2;
      }
      cli.frame_index = parsed;
    } else if (arg == "--draw") {
      const char *value = require_value("--draw");
      std::size_t parsed = 0;
      if (!value || !ParseSizeArgument(value, parsed)) {
        std::cerr << "--draw expects an integer\n";
        return 2;
      }
      cli.draw_index = parsed;
    } else if (arg == "--max-draws") {
      const char *value = require_value("--max-draws");
      if (!value || !ParseSizeArgument(value, cli.max_draws)) {
        std::cerr << "--max-draws expects an integer\n";
        return 2;
      }
    } else if (arg == "--top-shaders") {
      const char *value = require_value("--top-shaders");
      if (!value || !ParseSizeArgument(value, cli.top_shaders)) {
        std::cerr << "--top-shaders expects an integer\n";
        return 2;
      }
      cli.show_shader_usage = true;
    } else if (arg == "--backend") {
      const char *value = require_value("--backend");
      if (!value) {
        return 2;
      }
      cli.backend = value;
    } else if (arg == "--d3d12-output") {
      const char *value = require_value("--d3d12-output");
      if (!value) {
        return 2;
      }
      cli.d3d12_output_path = value;
    } else if (arg == "--d3d12-draws") {
      const char *value = require_value("--d3d12-draws");
      if (!value || !ParseSizeArgument(value, cli.d3d12_draw_limit)) {
        std::cerr << "--d3d12-draws expects an integer\n";
        return 2;
      }
    } else if (!arg.empty() && arg[0] != '-' && cli.capture_path.empty()) {
      cli.capture_path = std::string(arg);
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 2;
    }
  }

  if (cli.capture_path.empty()) {
    PrintHelp();
    return 2;
  }

  ReplayLoadOptions load_options;
  ReplayCapture capture;
  const bool clean = LoadReplayCapture(cli.capture_path, load_options, capture);
  const ReplayBackendKind backend_kind = ParseBackendKind(cli.backend);
  if (backend_kind == ReplayBackendKind::Unknown) {
    std::cerr << "unknown replay backend: " << cli.backend << "\n";
    return 2;
  }

  std::cout << "Native replay backend: " << cli.backend;
  if (backend_kind == ReplayBackendKind::Null) {
    std::cout << " (offline analysis only)\n";
  } else if (backend_kind == ReplayBackendKind::D3D12Diagnostic) {
    std::cout << " (offscreen D3D12 diagnostic renderer)\n";
  } else if (backend_kind == ReplayBackendKind::D3D12Real) {
    std::cout << " (resource-backed D3D12 renderer)\n";
  } else if (backend_kind == ReplayBackendKind::VulkanDiagnostic) {
    std::cout << " (Vulkan diagnostic renderer)\n";
  } else if (backend_kind == ReplayBackendKind::VulkanReal) {
    std::cout << " (resource-backed Vulkan renderer)\n";
  }

  if (cli.validate_only) {
    if (!clean) {
      for (const std::string &error : capture.errors) {
        std::cerr << error << "\n";
      }
      return 1;
    }
    std::cout << "Validation OK: " << capture.summary.total_events
              << " events, " << capture.frames.size() << " frames, "
              << capture.draws.size() << " draws\n";
    return 0;
  }

  if (backend_kind == ReplayBackendKind::D3D12Diagnostic) {
    std::string backend_error;
    if (!RunD3D12DiagnosticReplayBackend(capture, cli, backend_error)) {
      std::cerr << "D3D12 diagnostic replay failed: " << backend_error << "\n";
      return 1;
    }
    const std::filesystem::path output =
        cli.d3d12_output_path.empty()
            ? capture.path.parent_path() / "native-renderer-d3d12-replay.bmp"
            : cli.d3d12_output_path;
    std::cout << "D3D12 replay output: "
              << std::filesystem::absolute(output).string() << "\n";
  } else if (backend_kind == ReplayBackendKind::D3D12Real) {
    std::string backend_error;
    if (!RunD3D12RealReplayBackend(capture, cli, backend_error)) {
      std::cerr << "D3D12 real replay unavailable: " << backend_error << "\n";
      return 1;
    }
  } else if (backend_kind == ReplayBackendKind::VulkanDiagnostic ||
             backend_kind == ReplayBackendKind::VulkanReal) {
    std::cerr << "Vulkan replay backend unavailable: no Vulkan backend is "
                 "implemented in this tree yet\n";
    return 1;
  }

  if (cli.show_summary) {
    PrintReplaySummary(capture);
  }
  if (cli.frame_index) {
    std::cout << "\n";
    PrintFrameSummary(capture, *cli.frame_index);
  }
  if (cli.dump_draws || cli.draw_index) {
    std::cout << "\n";
    PrintDrawDump(capture, cli.frame_index, cli.draw_index, cli.max_draws);
  }
  if (cli.dump_constants) {
    std::cout << "\n";
    PrintConstantsDump(capture, cli.frame_index, cli.draw_index);
  }
  if (cli.show_resource_summary) {
    std::cout << "\n";
    PrintResourceSummary(capture);
  }
  if (cli.dump_indices) {
    if (!cli.draw_index) {
      std::cerr << "--dump-indices requires --draw <index>\n";
      return 2;
    }
    std::cout << "\n";
    PrintIndexDump(capture, *cli.draw_index);
  }
  if (cli.dump_vertices) {
    if (!cli.draw_index) {
      std::cerr << "--dump-vertices requires --draw <index>\n";
      return 2;
    }
    std::cout << "\n";
    PrintVertexDump(capture, *cli.draw_index);
  }
  if (cli.dump_bound_state) {
    if (!cli.draw_index) {
      std::cerr << "--dump-bound-state requires --draw <index>\n";
      return 2;
    }
    std::cout << "\n";
    PrintBoundStateDump(capture, *cli.draw_index);
  }
  if (cli.show_shader_usage) {
    std::cout << "\n";
    PrintShaderUsage(capture, cli.top_shaders);
  }
  if (cli.show_missing_shaders) {
    std::cout << "\n";
    PrintMissingShaders(capture);
  }

  return clean ? 0 : 1;
}

} // namespace bo2::native::replay
