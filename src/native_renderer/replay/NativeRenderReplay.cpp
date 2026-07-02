#include "NativeRenderReplay.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace bo2::native::replay {

namespace {

enum class JsonValueType {
  Null,
  String,
  Number,
  Bool,
  Array,
  Object,
};

struct JsonValue {
  JsonValueType type = JsonValueType::Null;
  std::string string_value;
  uint64_t number_value = 0;
  int64_t signed_number_value = 0;
  bool bool_value = false;
  std::vector<JsonValue> array_value;
  std::vector<std::pair<std::string, JsonValue>> object_value;
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
    if (text_[pos_] == '{') {
      out.type = JsonValueType::Object;
      return ParseObjectValue(out.object_value, error);
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
    if (text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
      out.type = JsonValueType::Number;
      return ParseNumber(out.number_value, out.signed_number_value, error);
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

  bool ParseNumber(uint64_t &out, int64_t &signed_out, std::string &error) {
    bool negative = false;
    if (text_[pos_] == '-') {
      negative = true;
      ++pos_;
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        error = "invalid number";
        return false;
      }
    }

    const std::size_t begin = pos_;
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
      ++pos_;
    }

    const auto number = text_.substr(begin, pos_ - begin);
    const char *first = number.data();
    const char *last = first + number.size();
    uint64_t magnitude = 0;
    const auto result = std::from_chars(first, last, magnitude, 10);
    if (result.ec != std::errc{} || result.ptr != last) {
      error = "invalid number";
      return false;
    }
    if (negative) {
      constexpr uint64_t kMinMagnitude =
          uint64_t(std::numeric_limits<int64_t>::max()) + 1;
      if (magnitude > kMinMagnitude) {
        error = "negative number out of range";
        return false;
      }
      signed_out = magnitude == kMinMagnitude
                       ? std::numeric_limits<int64_t>::min()
                       : -static_cast<int64_t>(magnitude);
      out = 0;
    } else {
      if (magnitude > uint64_t(std::numeric_limits<int64_t>::max())) {
        signed_out = std::numeric_limits<int64_t>::max();
      } else {
        signed_out = static_cast<int64_t>(magnitude);
      }
      out = magnitude;
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

  bool ParseObjectValue(std::vector<std::pair<std::string, JsonValue>> &out,
                        std::string &error) {
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
      out.emplace_back(std::move(key), std::move(value));
      SkipWhitespace();
      if (Consume('}')) {
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

const JsonValue *FindValue(
    const std::vector<std::pair<std::string, JsonValue>> &object,
    std::string_view key) {
  for (const auto &[member_key, value] : object) {
    if (member_key == key) {
      return &value;
    }
  }
  return nullptr;
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

template <typename Object>
uint64_t GetU64(const Object &object, std::string_view key,
                uint64_t default_value = 0) {
  const JsonValue *value = FindValue(object, key);
  if (!value) {
    return default_value;
  }
  if (value->type == JsonValueType::Number) {
    if (value->signed_number_value < 0) {
      return default_value;
    }
    return value->number_value;
  }
  if (value->type == JsonValueType::String) {
    uint64_t parsed = 0;
    return ParseIntegerText(value->string_value, parsed) ? parsed
                                                         : default_value;
  }
  return default_value;
}

template <typename Object>
int32_t GetI32(const Object &object, std::string_view key,
               int32_t default_value = 0) {
  const JsonValue *value = FindValue(object, key);
  if (!value) {
    return default_value;
  }
  if (value->type == JsonValueType::Number) {
    if (value->signed_number_value < std::numeric_limits<int32_t>::min() ||
        value->signed_number_value > std::numeric_limits<int32_t>::max()) {
      return default_value;
    }
    return static_cast<int32_t>(value->signed_number_value);
  }
  if (value->type == JsonValueType::String) {
    int base = 10;
    std::string_view text = value->string_value;
    bool negative = false;
    if (!text.empty() && text.front() == '-') {
      negative = true;
      text.remove_prefix(1);
    }
    if (text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
      text.remove_prefix(2);
      base = 16;
    }
    uint64_t magnitude = 0;
    const char *first = text.data();
    const char *last = first + text.size();
    const auto result = std::from_chars(first, last, magnitude, base);
    if (result.ec != std::errc{} || result.ptr != last) {
      return default_value;
    }
    if (negative) {
      if (magnitude > uint64_t(std::numeric_limits<int32_t>::max()) + 1) {
        return default_value;
      }
      return magnitude == uint64_t(std::numeric_limits<int32_t>::max()) + 1
                 ? std::numeric_limits<int32_t>::min()
                 : -static_cast<int32_t>(magnitude);
    }
    return magnitude > uint64_t(std::numeric_limits<int32_t>::max())
               ? default_value
               : static_cast<int32_t>(magnitude);
  }
  return default_value;
}

template <typename Object>
uint32_t GetU32(const Object &object, std::string_view key,
                uint32_t default_value = 0) {
  const uint64_t value = GetU64(object, key, default_value);
  return value > std::numeric_limits<uint32_t>::max()
             ? default_value
             : static_cast<uint32_t>(value);
}

template <typename Object>
bool GetBool(const Object &object, std::string_view key,
             bool default_value = false) {
  const JsonValue *value = FindValue(object, key);
  return value && value->type == JsonValueType::Bool ? value->bool_value
                                                     : default_value;
}

template <typename Object>
std::string GetString(const Object &object, std::string_view key,
                      std::string default_value = {}) {
  const JsonValue *value = FindValue(object, key);
  return value && value->type == JsonValueType::String
             ? value->string_value
             : std::move(default_value);
}

template <typename Object>
std::vector<uint32_t> GetU32Array(const Object &object,
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

template <typename Object>
std::vector<int32_t> GetI32Array(const Object &object, std::string_view key) {
  std::vector<int32_t> values;
  const JsonValue *value = FindValue(object, key);
  if (!value || value->type != JsonValueType::Array) {
    return values;
  }

  values.reserve(value->array_value.size());
  for (const JsonValue &element : value->array_value) {
    if (element.type == JsonValueType::Number) {
      if (element.number_value >= std::numeric_limits<int32_t>::min() &&
          element.number_value <= std::numeric_limits<int32_t>::max()) {
        values.push_back(static_cast<int32_t>(element.number_value));
      }
      continue;
    }
    if (element.type == JsonValueType::String) {
      uint64_t parsed = 0;
      if (ParseIntegerText(element.string_value, parsed) &&
          parsed <= static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        values.push_back(static_cast<int32_t>(parsed));
      }
    }
  }
  return values;
}

template <typename Object>
std::vector<uint8_t> GetU8Array(const Object &object,
                                std::string_view key) {
  std::vector<uint8_t> bytes;
  const std::vector<uint32_t> values = GetU32Array(object, key);
  bytes.reserve(values.size());
  for (uint32_t value : values) {
    if (value <= std::numeric_limits<uint8_t>::max()) {
      bytes.push_back(static_cast<uint8_t>(value));
    }
  }
  return bytes;
}

std::vector<VertexAttributeRecord> ParseVertexAttributes(
    const std::vector<std::pair<std::string, JsonValue>> &object) {
  std::vector<VertexAttributeRecord> attributes;
  const JsonValue *value = FindValue(object, "attributes");
  if (!value || value->type != JsonValueType::Array) {
    return attributes;
  }

  attributes.reserve(value->array_value.size());
  for (const JsonValue &element : value->array_value) {
    if (element.type != JsonValueType::Object) {
      continue;
    }
    const auto &attr_object = element.object_value;
    VertexAttributeRecord attribute{};
    attribute.data_format = GetU32(attr_object, "data_format");
    attribute.offset = GetI32(attr_object, "offset");
    attribute.offset_bytes =
        GetU32(attr_object, "offset_bytes",
               attribute.offset < 0 ? 0
                                    : static_cast<uint32_t>(attribute.offset) * 4);
    attribute.stride = GetU32(attr_object, "stride");
    attribute.stride_bytes =
        GetU32(attr_object, "stride_bytes", attribute.stride * 4);
    attribute.exp_adjust = GetI32(attr_object, "exp_adjust");
    attribute.prefetch_count = GetU32(attr_object, "prefetch_count");
    attribute.signed_rf_mode = GetU32(attr_object, "signed_rf_mode");
    attribute.is_index_rounded = GetBool(attr_object, "is_index_rounded");
    attribute.is_signed = GetBool(attr_object, "is_signed");
    attribute.is_integer = GetBool(attr_object, "is_integer");
    attributes.push_back(std::move(attribute));
  }
  return attributes;
}

std::vector<VertexFetchRecord> ParseVertexFetches(const JsonObject &object) {
  std::vector<VertexFetchRecord> fetches;
  const JsonValue *value = FindValue(object, "vertex_fetches");
  if (!value || value->type != JsonValueType::Array) {
    return fetches;
  }

  fetches.reserve(value->array_value.size());
  for (const JsonValue &element : value->array_value) {
    if (element.type != JsonValueType::Object) {
      continue;
    }
    const auto &fetch_object = element.object_value;
    VertexFetchRecord fetch{};
    fetch.fetch_constant = GetU32(fetch_object, "fetch_constant");
    fetch.dword_0 = GetU32(fetch_object, "dword_0");
    fetch.dword_1 = GetU32(fetch_object, "dword_1");
    fetch.type = GetU32(fetch_object, "type");
    fetch.address = GetU32(fetch_object, "address");
    fetch.address_bytes =
        GetU32(fetch_object, "address_bytes", fetch.address << 2);
    fetch.size_words = GetU32(fetch_object, "size_words");
    fetch.size_bytes =
        GetU32(fetch_object, "size_bytes", fetch.size_words << 2);
    fetch.endian = GetU32(fetch_object, "endian");
    fetch.stride_words = GetU32(fetch_object, "stride_words");
    fetch.stride_bytes =
        GetU32(fetch_object, "stride_bytes", fetch.stride_words << 2);
    fetch.attribute_count = GetU32(fetch_object, "attribute_count");
    fetch.captured_attribute_count =
        GetU32(fetch_object, "captured_attribute_count");
    fetch.attributes = ParseVertexAttributes(fetch_object);
    if (fetch.captured_attribute_count == 0 && !fetch.attributes.empty()) {
      fetch.captured_attribute_count =
          static_cast<uint32_t>(fetch.attributes.size());
    }
    fetch.payload_byte_count = GetU32(fetch_object, "payload_byte_count");
    fetch.payload_resource_byte_count =
        GetU32(fetch_object, "payload_resource_byte_count");
    fetch.payload_resource_path =
        GetString(fetch_object, "payload_resource_path");
    fetch.payload_bytes = GetU8Array(fetch_object, "payload_bytes");
    if (fetch.payload_byte_count == 0 && !fetch.payload_bytes.empty()) {
      fetch.payload_byte_count =
          static_cast<uint32_t>(fetch.payload_bytes.size());
    }
    fetch.payload_truncated = GetBool(fetch_object, "payload_truncated");
    fetch.payload_missing =
        GetBool(fetch_object, "payload_missing",
                !FindValue(fetch_object, "payload_bytes") &&
                    fetch.payload_resource_path.empty());
    if (!fetch.payload_bytes.empty()) {
      fetch.payload_missing = false;
    }
    fetches.push_back(std::move(fetch));
  }
  return fetches;
}

TextureFetchRecord ParseTextureFetchRecord(
    const std::vector<std::pair<std::string, JsonValue>> &fetch_object) {
  TextureFetchRecord fetch{};
  fetch.shader_type = GetU32(fetch_object, "shader_type");
  fetch.binding_index = GetU32(fetch_object, "binding_index");
  fetch.fetch_constant = GetU32(fetch_object, "fetch_constant");
  fetch.dwords = GetU32Array(fetch_object, "dwords");
  fetch.type = GetU32(fetch_object, "type");
  fetch.base_address = GetU32(fetch_object, "base_address");
  fetch.base_address_bytes =
      GetU32(fetch_object, "base_address_bytes", fetch.base_address << 12);
  fetch.mip_address = GetU32(fetch_object, "mip_address");
  fetch.mip_address_bytes =
      GetU32(fetch_object, "mip_address_bytes", fetch.mip_address << 12);
  fetch.pitch = GetU32(fetch_object, "pitch");
  fetch.tiled = GetBool(fetch_object, "tiled");
  fetch.format = GetU32(fetch_object, "format");
  fetch.endian = GetU32(fetch_object, "endian");
  fetch.request_size = GetU32(fetch_object, "request_size");
  fetch.stacked = GetBool(fetch_object, "stacked");
  fetch.width = GetU32(fetch_object, "width");
  fetch.height = GetU32(fetch_object, "height");
  fetch.depth_or_stack = GetU32(fetch_object, "depth_or_stack");
  fetch.num_format = GetU32(fetch_object, "num_format");
  fetch.swizzle = GetU32(fetch_object, "swizzle");
  fetch.exp_adjust = GetI32(fetch_object, "exp_adjust");
  fetch.clamp_modes_present = FindValue(fetch_object, "clamp_x") != nullptr &&
                              FindValue(fetch_object, "clamp_y") != nullptr &&
                              FindValue(fetch_object, "clamp_z") != nullptr;
  fetch.clamp_x = GetU32(fetch_object, "clamp_x");
  fetch.clamp_y = GetU32(fetch_object, "clamp_y");
  fetch.clamp_z = GetU32(fetch_object, "clamp_z");
  fetch.mag_filter = GetU32(fetch_object, "mag_filter");
  fetch.min_filter = GetU32(fetch_object, "min_filter");
  fetch.mip_filter = GetU32(fetch_object, "mip_filter");
  fetch.aniso_filter = GetU32(fetch_object, "aniso_filter");
  fetch.arbitrary_filter = GetU32(fetch_object, "arbitrary_filter");
  fetch.border_size = GetU32(fetch_object, "border_size");
  fetch.vol_mag_filter = GetU32(fetch_object, "vol_mag_filter");
  fetch.vol_min_filter = GetU32(fetch_object, "vol_min_filter");
  fetch.mip_min_level = GetU32(fetch_object, "mip_min_level");
  fetch.mip_max_level = GetU32(fetch_object, "mip_max_level");
  fetch.lod_bias = GetI32(fetch_object, "lod_bias");
  fetch.grad_exp_adjust_h = GetI32(fetch_object, "grad_exp_adjust_h");
  fetch.grad_exp_adjust_v = GetI32(fetch_object, "grad_exp_adjust_v");
  fetch.border_color = GetU32(fetch_object, "border_color");
  fetch.force_bc_w_to_max = GetU32(fetch_object, "force_bc_w_to_max");
  fetch.tri_clamp = GetU32(fetch_object, "tri_clamp");
  fetch.aniso_bias = GetI32(fetch_object, "aniso_bias");
  fetch.dimension = GetU32(fetch_object, "dimension");
  fetch.packed_mips = GetBool(fetch_object, "packed_mips");
  fetch.payload_byte_count = GetU32(fetch_object, "payload_byte_count");
  fetch.payload_resource_byte_count =
      GetU32(fetch_object, "payload_resource_byte_count");
  fetch.payload_resource_path =
      GetString(fetch_object, "payload_resource_path");
  fetch.payload_bytes = GetU8Array(fetch_object, "payload_bytes");
  if (fetch.payload_byte_count == 0 && !fetch.payload_bytes.empty()) {
    fetch.payload_byte_count =
        static_cast<uint32_t>(fetch.payload_bytes.size());
  }
  fetch.payload_truncated = GetBool(fetch_object, "payload_truncated");
  fetch.payload_missing =
      GetBool(fetch_object, "payload_missing",
              !FindValue(fetch_object, "payload_bytes"));
  if (!fetch.payload_bytes.empty()) {
    fetch.payload_missing = false;
  }
  return fetch;
}

std::vector<TextureFetchRecord> ParseTextureFetches(const JsonObject &object) {
  std::vector<TextureFetchRecord> fetches;
  const JsonValue *value = FindValue(object, "texture_fetches");
  if (!value || value->type != JsonValueType::Array) {
    return fetches;
  }

  fetches.reserve(value->array_value.size());
  for (const JsonValue &element : value->array_value) {
    if (element.type != JsonValueType::Object) {
      continue;
    }
    fetches.push_back(ParseTextureFetchRecord(element.object_value));
  }
  return fetches;
}

RenderTargetPayloadRecord ParseRenderTargetPayload(
    const std::vector<std::pair<std::string, JsonValue>> &object) {
  RenderTargetPayloadRecord payload{};
  payload.target = GetU32(object, "target");
  payload.base = GetU32(object, "base");
  payload.payload_requested_byte_count =
      GetU32(object, "payload_requested_byte_count");
  payload.payload_offset_bytes = GetU32(object, "payload_offset_bytes");
  payload.payload_byte_count = GetU32(object, "payload_byte_count");
  payload.payload_resource_byte_count =
      GetU32(object, "payload_resource_byte_count");
  payload.payload_resource_path = GetString(object, "payload_resource_path");
  payload.payload_bytes = GetU8Array(object, "payload_bytes");
  if (payload.payload_byte_count == 0 && !payload.payload_bytes.empty()) {
    payload.payload_byte_count =
        static_cast<uint32_t>(payload.payload_bytes.size());
  }
  payload.payload_truncated = GetBool(object, "payload_truncated");
  payload.payload_missing =
      GetBool(object, "payload_missing", !FindValue(object, "payload_bytes"));
  if (!payload.payload_bytes.empty()) {
    payload.payload_missing = false;
  }
  return payload;
}

std::vector<RenderTargetPayloadRecord> ParseRenderTargetPayloads(
    const std::vector<std::pair<std::string, JsonValue>> &object) {
  std::vector<RenderTargetPayloadRecord> payloads;
  const JsonValue *value = FindValue(object, "color_target_payloads");
  if (!value || value->type != JsonValueType::Array) {
    return payloads;
  }

  payloads.reserve(value->array_value.size());
  for (const JsonValue &element : value->array_value) {
    if (element.type != JsonValueType::Object) {
      continue;
    }
    payloads.push_back(ParseRenderTargetPayload(element.object_value));
  }
  return payloads;
}

RenderTargetPayloadRecord ParseDepthTargetPayload(
    const std::vector<std::pair<std::string, JsonValue>> &object) {
  const JsonValue *value = FindValue(object, "depth_target_payload");
  if (!value || value->type != JsonValueType::Object) {
    return RenderTargetPayloadRecord{};
  }
  RenderTargetPayloadRecord payload =
      ParseRenderTargetPayload(value->object_value);
  payload.target = 0;
  return payload;
}

RenderStateRecord ParseRenderState(const JsonObject &object) {
  RenderStateRecord state{};
  const JsonValue *value = FindValue(object, "render_state");
  if (!value || value->type != JsonValueType::Object) {
    return state;
  }
  const auto &state_object = value->object_value;
  state.present = true;
  state.rb_modecontrol = GetU32(state_object, "rb_modecontrol");
  state.rb_surface_info = GetU32(state_object, "rb_surface_info");
  state.rb_colorcontrol = GetU32(state_object, "rb_colorcontrol");
  state.rb_color_mask = GetU32(state_object, "rb_color_mask");
  state.rb_depthcontrol = GetU32(state_object, "rb_depthcontrol");
  state.rb_stencilrefmask = GetU32(state_object, "rb_stencilrefmask");
  state.rb_stencilrefmask_bf = GetU32(state_object, "rb_stencilrefmask_bf");
  state.rb_depth_info = GetU32(state_object, "rb_depth_info");
  state.rb_alpha_ref = GetU32(state_object, "rb_alpha_ref");
  state.pa_sc_screen_scissor_tl =
      GetU32(state_object, "pa_sc_screen_scissor_tl");
  state.pa_sc_screen_scissor_br =
      GetU32(state_object, "pa_sc_screen_scissor_br");
  state.pa_sc_window_offset = GetU32(state_object, "pa_sc_window_offset");
  state.pa_sc_window_scissor_tl =
      GetU32(state_object, "pa_sc_window_scissor_tl");
  state.pa_sc_window_scissor_br =
      GetU32(state_object, "pa_sc_window_scissor_br");
  state.pa_cl_clip_cntl = GetU32(state_object, "pa_cl_clip_cntl");
  state.pa_cl_vte_cntl = GetU32(state_object, "pa_cl_vte_cntl");
  state.pa_su_sc_mode_cntl = GetU32(state_object, "pa_su_sc_mode_cntl");
  state.pa_su_vtx_cntl = GetU32(state_object, "pa_su_vtx_cntl");
  state.sq_program_cntl = GetU32(state_object, "sq_program_cntl");
  state.sq_context_misc = GetU32(state_object, "sq_context_misc");
  state.viewport_registers =
      GetU32Array(state_object, "viewport_registers");
  state.rb_color_info = GetU32Array(state_object, "rb_color_info");
  state.rb_blendcontrol = GetU32Array(state_object, "rb_blendcontrol");
  state.surface_pitch = GetU32(state_object, "surface_pitch");
  state.msaa_samples = GetU32(state_object, "msaa_samples");
  state.depth_base = GetU32(state_object, "depth_base");
  state.depth_format = GetU32(state_object, "depth_format");
  state.color_base = GetU32Array(state_object, "color_base");
  state.color_format = GetU32Array(state_object, "color_format");
  state.color_exp_bias = GetI32Array(state_object, "color_exp_bias");
  state.color_target_payloads = ParseRenderTargetPayloads(state_object);
  state.depth_target_payload = ParseDepthTargetPayload(state_object);
  state.depth_test_enable = GetBool(state_object, "depth_test_enable");
  state.depth_write_enable = GetBool(state_object, "depth_write_enable");
  state.stencil_enable = GetBool(state_object, "stencil_enable");
  state.depth_func = GetU32(state_object, "depth_func");
  state.cull_mode = GetU32(state_object, "cull_mode");
  state.fill_mode = GetU32(state_object, "fill_mode");
  state.front_face = GetU32(state_object, "front_face");
  return state;
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
    event.shader.payload_dword_count = GetU32(object, "payload_dword_count");
    event.shader.dwords = GetU32Array(object, "dwords");
    if (event.shader.dwords.empty()) {
      event.shader.dwords = GetU32Array(object, "payload_dwords");
    }
    if (event.shader.payload_dword_count == 0 &&
        !event.shader.dwords.empty()) {
      event.shader.payload_dword_count =
          static_cast<uint32_t>(event.shader.dwords.size());
    }
    event.shader.payload_truncated = GetBool(object, "payload_truncated");
    event.shader.payload_missing = GetBool(
        object, "payload_missing",
        !FindValue(object, "dwords") && !FindValue(object, "payload_dwords"));
    if (!event.shader.dwords.empty()) {
      event.shader.payload_missing = false;
    }
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
    event.draw.index_payload_byte_count =
        GetU32(object, "index_payload_byte_count");
    event.draw.index_bytes = GetU8Array(object, "index_bytes");
    if (event.draw.index_bytes.empty()) {
      event.draw.index_bytes = GetU8Array(object, "index_payload_bytes");
    }
    if (event.draw.index_payload_byte_count == 0 &&
        !event.draw.index_bytes.empty()) {
      event.draw.index_payload_byte_count =
          static_cast<uint32_t>(event.draw.index_bytes.size());
    }
    event.draw.index_payload_truncated =
        GetBool(object, "index_payload_truncated");
    event.draw.index_payload_missing = GetBool(
        object, "index_payload_missing",
        !FindValue(object, "index_bytes") &&
            !FindValue(object, "index_payload_bytes"));
    if (!event.draw.index_bytes.empty()) {
      event.draw.index_payload_missing = false;
    }
    event.draw.vertex_fetch_count = GetU32(object, "vertex_fetch_count");
    event.draw.vertex_fetch_truncated =
        GetBool(object, "vertex_fetch_truncated");
    event.draw.vertex_fetch_state_present =
        FindValue(object, "vertex_fetch_count") ||
        FindValue(object, "vertex_fetches");
    event.draw.vertex_fetches = ParseVertexFetches(object);
    if (event.draw.vertex_fetch_count == 0 &&
        !event.draw.vertex_fetches.empty()) {
      event.draw.vertex_fetch_count =
          static_cast<uint32_t>(event.draw.vertex_fetches.size());
    }
    if (event.draw.vertex_fetches.size() < event.draw.vertex_fetch_count) {
      event.draw.vertex_fetch_truncated = true;
    }
    event.draw.texture_fetch_count = GetU32(object, "texture_fetch_count");
    event.draw.texture_fetch_truncated =
        GetBool(object, "texture_fetch_truncated");
    event.draw.texture_fetch_state_present =
        FindValue(object, "texture_fetch_count") ||
        FindValue(object, "texture_fetches");
    event.draw.texture_fetches = ParseTextureFetches(object);
    if (event.draw.texture_fetch_count == 0 &&
        !event.draw.texture_fetches.empty()) {
      event.draw.texture_fetch_count =
          static_cast<uint32_t>(event.draw.texture_fetches.size());
    }
    if (event.draw.texture_fetches.size() < event.draw.texture_fetch_count) {
      event.draw.texture_fetch_truncated = true;
    }
    event.draw.float_constant_dword_count =
        GetU32(object, "float_constant_dword_count");
    event.draw.float_constant_resource_byte_count =
        GetU32(object, "float_constant_resource_byte_count");
    event.draw.float_constant_resource_path =
        GetString(object, "float_constant_resource_path");
    event.draw.float_constant_dwords =
        GetU32Array(object, "float_constant_dwords");
    if (event.draw.float_constant_dword_count == 0 &&
        !event.draw.float_constant_dwords.empty()) {
      event.draw.float_constant_dword_count =
          static_cast<uint32_t>(event.draw.float_constant_dwords.size());
    }
    event.draw.float_constants_missing = GetBool(
        object, "float_constants_missing",
        !FindValue(object, "float_constant_dwords"));
    if (!event.draw.float_constant_dwords.empty()) {
      event.draw.float_constants_missing = false;
    }
    event.draw.render_state = ParseRenderState(object);
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
    event.swap.frontbuffer_payload_requested_byte_count =
        GetU32(object, "frontbuffer_payload_requested_byte_count");
    event.swap.frontbuffer_payload_byte_count =
        GetU32(object, "frontbuffer_payload_byte_count");
    event.swap.frontbuffer_payload_resource_byte_count =
        GetU32(object, "frontbuffer_payload_resource_byte_count");
    event.swap.frontbuffer_payload_resource_path =
        GetString(object, "frontbuffer_payload_resource_path");
    event.swap.frontbuffer_payload_bytes =
        GetU8Array(object, "frontbuffer_payload_bytes");
    if (event.swap.frontbuffer_payload_byte_count == 0 &&
        !event.swap.frontbuffer_payload_bytes.empty()) {
      event.swap.frontbuffer_payload_byte_count =
          static_cast<uint32_t>(event.swap.frontbuffer_payload_bytes.size());
    }
    event.swap.frontbuffer_payload_truncated =
        GetBool(object, "frontbuffer_payload_truncated");
    event.swap.frontbuffer_payload_missing =
        GetBool(object, "frontbuffer_payload_missing",
                !FindValue(object, "frontbuffer_payload_bytes"));
    if (!event.swap.frontbuffer_payload_bytes.empty()) {
      event.swap.frontbuffer_payload_missing = false;
    }
    event.swap.frontbuffer_fetch_valid =
        GetBool(object, "frontbuffer_fetch_valid");
    if (const JsonValue *frontbuffer_fetch =
            FindValue(object, "frontbuffer_fetch");
        frontbuffer_fetch &&
        frontbuffer_fetch->type == JsonValueType::Object) {
      event.swap.frontbuffer_fetch =
          ParseTextureFetchRecord(frontbuffer_fetch->object_value);
      event.swap.frontbuffer_fetch_valid = true;
    }
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
  case CaptureEventType::ShaderRecordProbe:
    event.shader_probe.event = GetU64(object, "event");
    event.shader_probe.function = GetString(object, "function");
    event.shader_probe.function_address = GetU32(object, "function_address");
    event.shader_probe.link_register = GetU64(object, "link_register");
    event.shader_probe.r3 = GetU32(object, "r3");
    event.shader_probe.r4 = GetU32(object, "r4");
    event.shader_probe.r5 = GetU32(object, "r5");
    event.shader_probe.r6 = GetU32(object, "r6");
    event.shader_probe.r7 = GetU32(object, "r7");
    event.shader_probe.r8 = GetU32(object, "r8");
    event.shader_probe.r9 = GetU32(object, "r9");
    event.shader_probe.r10 = GetU32(object, "r10");
    event.shader_probe.r28 = GetU32(object, "r28");
    event.shader_probe.r29 = GetU32(object, "r29");
    event.shader_probe.r30 = GetU32(object, "r30");
    event.shader_probe.r31 = GetU32(object, "r31");
    event.shader_probe.command_buffer_object =
        GetU32(object, "command_buffer_object");
    event.shader_probe.write_begin = GetU32(object, "write_begin");
    event.shader_probe.write_end = GetU32(object, "write_end");
    event.shader_probe.write_limit_begin =
        GetU32(object, "write_limit_begin");
    event.shader_probe.write_limit_end = GetU32(object, "write_limit_end");
    event.shader_probe.return_value = GetU32(object, "return_value");
    event.shader_probe.primary_address = GetU32(object, "primary_address");
    event.shader_probe.primary_dword_count_hint =
        GetU32(object, "primary_dword_count_hint");
    event.shader_probe.primary_dword_count =
        GetU32(object, "primary_dword_count");
    event.shader_probe.primary_dwords = GetU32Array(object, "primary_dwords");
    event.shader_probe.primary_truncated =
        GetBool(object, "primary_truncated");
    event.shader_probe.primary_missing = GetBool(object, "primary_missing");
    event.shader_probe.secondary_address =
        GetU32(object, "secondary_address");
    event.shader_probe.secondary_dword_count =
        GetU32(object, "secondary_dword_count");
    event.shader_probe.secondary_dwords =
        GetU32Array(object, "secondary_dwords");
    event.shader_probe.secondary_truncated =
        GetBool(object, "secondary_truncated");
    event.shader_probe.secondary_missing =
        GetBool(object, "secondary_missing");
    event.shader_probe.tertiary_address = GetU32(object, "tertiary_address");
    event.shader_probe.tertiary_dword_count =
        GetU32(object, "tertiary_dword_count");
    event.shader_probe.tertiary_dwords =
        GetU32Array(object, "tertiary_dwords");
    event.shader_probe.tertiary_truncated =
        GetBool(object, "tertiary_truncated");
    event.shader_probe.tertiary_missing =
        GetBool(object, "tertiary_missing");
    event.shader_probe.quaternary_address =
        GetU32(object, "quaternary_address");
    event.shader_probe.quaternary_dword_count =
        GetU32(object, "quaternary_dword_count");
    event.shader_probe.quaternary_dwords =
        GetU32Array(object, "quaternary_dwords");
    event.shader_probe.quaternary_truncated =
        GetBool(object, "quaternary_truncated");
    event.shader_probe.quaternary_missing =
        GetBool(object, "quaternary_missing");
    event.shader_probe.heap_candidate_address =
        GetU32(object, "heap_candidate_address");
    event.shader_probe.heap_candidate_dword_count =
        GetU32(object, "heap_candidate_dword_count");
    event.shader_probe.heap_candidate_dwords =
        GetU32Array(object, "heap_candidate_dwords");
    event.shader_probe.heap_candidate_truncated =
        GetBool(object, "heap_candidate_truncated");
    event.shader_probe.heap_candidate_missing =
        GetBool(object, "heap_candidate_missing");
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

bool ReadSidecarResource(const std::filesystem::path &path,
                         std::vector<uint8_t> &bytes,
                         std::string &error) {
  bytes.clear();
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    error = "could not open " + path.string();
    return false;
  }

  const std::streamoff size = file.tellg();
  if (size < 0) {
    error = "could not size " + path.string();
    return false;
  }

  constexpr std::streamoff kMaxSidecarResourceBytes = 64ll * 1024ll * 1024ll;
  if (size > kMaxSidecarResourceBytes) {
    error = "sidecar too large " + path.string() + " size=" +
            std::to_string(static_cast<uint64_t>(size));
    return false;
  }

  bytes.resize(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!bytes.empty() &&
      !file.read(reinterpret_cast<char *>(bytes.data()), size)) {
    error = "could not read " + path.string();
    bytes.clear();
    return false;
  }
  return true;
}

void LoadVertexPayloadSidecars(ReplayCapture &capture,
                               const ReplayLoadOptions &options) {
  const std::filesystem::path base_dir = capture.path.parent_path();
  for (CaptureEvent &event : capture.events) {
    if (event.type != CaptureEventType::PM4Draw) {
      continue;
    }

    for (VertexFetchRecord &fetch : event.draw.vertex_fetches) {
      if (fetch.payload_resource_path.empty()) {
        continue;
      }

      std::filesystem::path resource_path(fetch.payload_resource_path);
      if (resource_path.is_relative()) {
        resource_path = base_dir / resource_path;
      }

      std::vector<uint8_t> bytes;
      std::string error;
      if (!ReadSidecarResource(resource_path, bytes, error)) {
        AddWarning(capture, options,
                   "vertex payload sidecar load failed for seq " +
                       std::to_string(event.seq) + ": " + error);
        continue;
      }

      if (fetch.payload_resource_byte_count != 0 &&
          fetch.payload_resource_byte_count != bytes.size()) {
        AddWarning(capture, options,
                   "vertex payload sidecar byte count mismatch for seq " +
                       std::to_string(event.seq) + ": expected " +
                       std::to_string(fetch.payload_resource_byte_count) +
                       " got " + std::to_string(bytes.size()));
      }

      fetch.payload_bytes = std::move(bytes);
      fetch.payload_loaded_from_resource = true;
      fetch.payload_missing = false;
      if (fetch.payload_resource_byte_count == 0) {
        fetch.payload_resource_byte_count =
            static_cast<uint32_t>(fetch.payload_bytes.size());
      }
      if (fetch.payload_byte_count == 0 ||
          fetch.payload_byte_count < fetch.payload_bytes.size()) {
        fetch.payload_byte_count =
            static_cast<uint32_t>(fetch.payload_bytes.size());
      }
      if (fetch.payload_byte_count <= fetch.payload_bytes.size()) {
        fetch.payload_truncated = false;
      }
    }
  }
}

void LoadTexturePayloadSidecars(ReplayCapture &capture,
                                const ReplayLoadOptions &options) {
  const std::filesystem::path base_dir = capture.path.parent_path();
  for (CaptureEvent &event : capture.events) {
    if (event.type != CaptureEventType::PM4Draw) {
      continue;
    }

    for (TextureFetchRecord &fetch : event.draw.texture_fetches) {
      if (fetch.payload_resource_path.empty()) {
        continue;
      }

      std::filesystem::path resource_path(fetch.payload_resource_path);
      if (resource_path.is_relative()) {
        resource_path = base_dir / resource_path;
      }

      std::vector<uint8_t> bytes;
      std::string error;
      if (!ReadSidecarResource(resource_path, bytes, error)) {
        AddWarning(capture, options,
                   "texture payload sidecar load failed for seq " +
                       std::to_string(event.seq) + ": " + error);
        continue;
      }

      if (fetch.payload_resource_byte_count != 0 &&
          fetch.payload_resource_byte_count != bytes.size()) {
        AddWarning(capture, options,
                   "texture payload sidecar byte count mismatch for seq " +
                       std::to_string(event.seq) + ": expected " +
                       std::to_string(fetch.payload_resource_byte_count) +
                       " got " + std::to_string(bytes.size()));
      }

      fetch.payload_bytes = std::move(bytes);
      fetch.payload_loaded_from_resource = true;
      fetch.payload_missing = false;
      if (fetch.payload_resource_byte_count == 0) {
        fetch.payload_resource_byte_count =
            static_cast<uint32_t>(fetch.payload_bytes.size());
      }
      if (fetch.payload_byte_count == 0) {
        fetch.payload_byte_count =
            static_cast<uint32_t>(fetch.payload_bytes.size());
      }
    }
  }
}

void LoadFloatConstantSidecars(ReplayCapture &capture,
                               const ReplayLoadOptions &options) {
  const std::filesystem::path base_dir = capture.path.parent_path();
  for (CaptureEvent &event : capture.events) {
    if (event.type != CaptureEventType::PM4Draw ||
        event.draw.float_constant_resource_path.empty()) {
      continue;
    }

    std::filesystem::path resource_path(event.draw.float_constant_resource_path);
    if (resource_path.is_relative()) {
      resource_path = base_dir / resource_path;
    }

    std::vector<uint8_t> bytes;
    std::string error;
    if (!ReadSidecarResource(resource_path, bytes, error)) {
      AddWarning(capture, options,
                 "float constant sidecar load failed for seq " +
                     std::to_string(event.seq) + ": " + error);
      continue;
    }

    if (event.draw.float_constant_resource_byte_count != 0 &&
        event.draw.float_constant_resource_byte_count != bytes.size()) {
      AddWarning(capture, options,
                 "float constant sidecar byte count mismatch for seq " +
                     std::to_string(event.seq) + ": expected " +
                     std::to_string(
                         event.draw.float_constant_resource_byte_count) +
                     " got " + std::to_string(bytes.size()));
    }

    const std::size_t dword_count = bytes.size() / sizeof(uint32_t);
    event.draw.float_constant_dwords.resize(dword_count);
    if (!event.draw.float_constant_dwords.empty()) {
      std::memcpy(event.draw.float_constant_dwords.data(), bytes.data(),
                  dword_count * sizeof(uint32_t));
    }
    event.draw.float_constants_loaded_from_resource = true;
    event.draw.float_constants_missing = false;
    if (event.draw.float_constant_dword_count == 0 ||
        event.draw.float_constant_dword_count <
            event.draw.float_constant_dwords.size()) {
      event.draw.float_constant_dword_count = static_cast<uint32_t>(
          event.draw.float_constant_dwords.size());
    }
    if (event.draw.float_constant_resource_byte_count == 0) {
      event.draw.float_constant_resource_byte_count =
          static_cast<uint32_t>(bytes.size());
    }
  }
}

void LoadFrontbufferPayloadSidecars(ReplayCapture &capture,
                                    const ReplayLoadOptions &options) {
  const std::filesystem::path base_dir = capture.path.parent_path();
  for (CaptureEvent &event : capture.events) {
    if (event.type != CaptureEventType::PM4Swap ||
        event.swap.frontbuffer_payload_resource_path.empty()) {
      continue;
    }

    std::filesystem::path resource_path(
        event.swap.frontbuffer_payload_resource_path);
    if (resource_path.is_relative()) {
      resource_path = base_dir / resource_path;
    }

    std::vector<uint8_t> bytes;
    std::string error;
    if (!ReadSidecarResource(resource_path, bytes, error)) {
      AddWarning(capture, options,
                 "frontbuffer payload sidecar load failed for seq " +
                     std::to_string(event.seq) + ": " + error);
      continue;
    }

    if (event.swap.frontbuffer_payload_resource_byte_count != 0 &&
        event.swap.frontbuffer_payload_resource_byte_count != bytes.size()) {
      AddWarning(capture, options,
                 "frontbuffer payload sidecar byte count mismatch for seq " +
                     std::to_string(event.seq) + ": expected " +
                     std::to_string(
                         event.swap.frontbuffer_payload_resource_byte_count) +
                     " got " + std::to_string(bytes.size()));
    }

    event.swap.frontbuffer_payload_bytes = std::move(bytes);
    event.swap.frontbuffer_payload_loaded_from_resource = true;
    event.swap.frontbuffer_payload_missing = false;
    if (event.swap.frontbuffer_payload_resource_byte_count == 0) {
      event.swap.frontbuffer_payload_resource_byte_count =
          static_cast<uint32_t>(
              event.swap.frontbuffer_payload_bytes.size());
    }
    if (event.swap.frontbuffer_payload_byte_count == 0) {
      event.swap.frontbuffer_payload_byte_count =
          static_cast<uint32_t>(
              event.swap.frontbuffer_payload_bytes.size());
    }
  }
}

void LoadRenderTargetPayloadSidecar(
    ReplayCapture &capture, const ReplayLoadOptions &options,
    const std::filesystem::path &base_dir, uint64_t seq,
    std::string_view label, RenderTargetPayloadRecord &payload) {
  if (payload.payload_resource_path.empty()) {
    return;
  }

  std::filesystem::path resource_path(payload.payload_resource_path);
  if (resource_path.is_relative()) {
    resource_path = base_dir / resource_path;
  }

  std::vector<uint8_t> bytes;
  std::string error;
  if (!ReadSidecarResource(resource_path, bytes, error)) {
    AddWarning(capture, options,
               std::string(label) + " sidecar load failed for seq " +
                   std::to_string(seq) + ": " + error);
    return;
  }

  if (payload.payload_resource_byte_count != 0 &&
      payload.payload_resource_byte_count != bytes.size()) {
    AddWarning(capture, options,
               std::string(label) + " sidecar byte count mismatch for seq " +
                   std::to_string(seq) + ": expected " +
                   std::to_string(payload.payload_resource_byte_count) +
                   " got " + std::to_string(bytes.size()));
  }

  payload.payload_bytes = std::move(bytes);
  payload.payload_loaded_from_resource = true;
  payload.payload_missing = false;
  if (payload.payload_resource_byte_count == 0) {
    payload.payload_resource_byte_count =
        static_cast<uint32_t>(payload.payload_bytes.size());
  }
  if (payload.payload_byte_count == 0) {
    payload.payload_byte_count =
        static_cast<uint32_t>(payload.payload_bytes.size());
  }
}

void LoadRenderTargetPayloadSidecars(ReplayCapture &capture,
                                     const ReplayLoadOptions &options) {
  const std::filesystem::path base_dir = capture.path.parent_path();
  for (CaptureEvent &event : capture.events) {
    if (event.type != CaptureEventType::PM4Draw ||
        !event.draw.render_state.present) {
      continue;
    }

    for (RenderTargetPayloadRecord &payload :
         event.draw.render_state.color_target_payloads) {
      LoadRenderTargetPayloadSidecar(capture, options, base_dir, event.seq,
                                     "color target payload", payload);
    }
    LoadRenderTargetPayloadSidecar(capture, options, base_dir, event.seq,
                                   "depth target payload",
                                   event.draw.render_state.depth_target_payload);
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
      if (event.shader.payload_missing) {
        ++capture.summary.shader_uploads_missing_payload;
      } else {
        ++capture.summary.shader_uploads_with_payload;
        capture.summary.shader_payload_dwords += event.shader.dwords.size();
      }
      if (event.shader.payload_truncated) {
        ++capture.summary.shader_payload_truncated;
      }
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
      if (event.draw.indexed) {
        if (event.draw.index_payload_missing || event.draw.index_bytes.empty()) {
          ++capture.summary.index_buffer_snapshots_missing;
        } else {
          ++capture.summary.index_buffer_snapshots;
          capture.summary.index_payload_bytes += event.draw.index_bytes.size();
        }
        if (event.draw.index_payload_truncated) {
          ++capture.summary.index_payload_truncated;
        }
      }
      if (!event.draw.vertex_fetch_state_present) {
        ++capture.summary.draws_missing_vertex_fetch_state;
      } else if (event.draw.vertex_fetches.empty()) {
        ++capture.summary.draws_missing_vertex_fetch;
      } else {
        ++capture.summary.draws_with_vertex_fetch;
        capture.summary.vertex_fetch_records +=
            event.draw.vertex_fetches.size();
        for (const VertexFetchRecord &fetch : event.draw.vertex_fetches) {
          if (fetch.payload_missing || fetch.payload_bytes.empty()) {
            ++capture.summary.vertex_buffer_snapshots_missing;
          } else {
            ++capture.summary.vertex_buffer_snapshots;
            capture.summary.vertex_payload_bytes += fetch.payload_bytes.size();
          }
          if (fetch.payload_truncated) {
            ++capture.summary.vertex_payload_truncated;
          }
        }
      }
      if (!event.draw.texture_fetch_state_present) {
        ++capture.summary.draws_missing_texture_fetch_state;
      } else if (event.draw.texture_fetches.empty()) {
        ++capture.summary.draws_missing_texture_fetch;
      } else {
        ++capture.summary.draws_with_texture_fetch;
        capture.summary.texture_fetch_records +=
            event.draw.texture_fetches.size();
        for (const TextureFetchRecord &fetch : event.draw.texture_fetches) {
          if (fetch.clamp_modes_present) {
            ++capture.summary.texture_fetches_with_clamp_modes;
          } else {
            ++capture.summary.texture_fetches_missing_clamp_modes;
          }
          if (fetch.payload_missing || fetch.payload_bytes.empty()) {
            ++capture.summary.texture_snapshots_missing;
          } else {
            ++capture.summary.texture_snapshots;
            capture.summary.texture_payload_bytes += fetch.payload_bytes.size();
          }
          if (!fetch.payload_resource_path.empty()) {
            ++capture.summary.texture_payload_sidecars;
            capture.summary.texture_payload_sidecar_bytes +=
                fetch.payload_bytes.size();
          }
          if (fetch.payload_truncated) {
            ++capture.summary.texture_payload_truncated;
          }
        }
      }
      if (event.draw.render_state.present) {
        ++capture.summary.draws_with_render_state;
        for (const RenderTargetPayloadRecord &payload :
             event.draw.render_state.color_target_payloads) {
          if (payload.base == 0 &&
              payload.payload_requested_byte_count == 0 &&
              payload.payload_byte_count == 0 &&
              payload.payload_bytes.empty()) {
            continue;
          }
          if (payload.payload_missing || payload.payload_bytes.empty()) {
            ++capture.summary.color_target_snapshots_missing;
          } else {
            ++capture.summary.color_target_snapshots;
            capture.summary.color_target_payload_bytes +=
                payload.payload_bytes.size();
          }
          if (!payload.payload_resource_path.empty()) {
            ++capture.summary.color_target_payload_sidecars;
            capture.summary.color_target_payload_sidecar_bytes +=
                payload.payload_bytes.size();
          }
          if (payload.payload_truncated) {
            ++capture.summary.color_target_payload_truncated;
          }
        }
        const RenderTargetPayloadRecord &depth_payload =
            event.draw.render_state.depth_target_payload;
        if (depth_payload.base != 0 ||
            depth_payload.payload_requested_byte_count != 0 ||
            depth_payload.payload_byte_count != 0 ||
            !depth_payload.payload_bytes.empty()) {
          if (depth_payload.payload_missing ||
              depth_payload.payload_bytes.empty()) {
            ++capture.summary.depth_target_snapshots_missing;
          } else {
            ++capture.summary.depth_target_snapshots;
            capture.summary.depth_target_payload_bytes +=
                depth_payload.payload_bytes.size();
          }
          if (!depth_payload.payload_resource_path.empty()) {
            ++capture.summary.depth_target_payload_sidecars;
            capture.summary.depth_target_payload_sidecar_bytes +=
                depth_payload.payload_bytes.size();
          }
          if (depth_payload.payload_truncated) {
            ++capture.summary.depth_target_payload_truncated;
          }
        }
      } else {
        ++capture.summary.draws_missing_render_state;
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
      if (event.swap.frontbuffer_payload_missing ||
          event.swap.frontbuffer_payload_bytes.empty()) {
        ++capture.summary.frontbuffer_snapshots_missing;
      } else {
        ++capture.summary.frontbuffer_snapshots;
        capture.summary.frontbuffer_payload_bytes +=
            event.swap.frontbuffer_payload_bytes.size();
      }
      if (!event.swap.frontbuffer_payload_resource_path.empty()) {
        ++capture.summary.frontbuffer_payload_sidecars;
        capture.summary.frontbuffer_payload_sidecar_bytes +=
            event.swap.frontbuffer_payload_bytes.size();
      }
      if (event.swap.frontbuffer_payload_truncated) {
        ++capture.summary.frontbuffer_payload_truncated;
      }
      if (current_frame) {
        ++capture.frames[*current_frame].swap_count;
      }
      break;
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

  if (capture.summary.index_buffer_snapshots_missing != 0) {
    capture.errors.push_back(
        "indexed draws missing index snapshots: " +
        std::to_string(capture.summary.index_buffer_snapshots_missing));
  }
  if (capture.summary.draws_missing_vertex_fetch_state != 0) {
    capture.errors.push_back(
        "draws missing vertex fetch state fields: " +
        std::to_string(capture.summary.draws_missing_vertex_fetch_state));
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

std::string IndexPayloadStatus(const PM4DrawRecord &draw) {
  std::ostringstream os;
  if (draw.index_payload_missing || draw.index_bytes.empty()) {
    os << "payload=missing";
    return os.str();
  }
  os << "payload=" << draw.index_bytes.size() << "/" << draw.index_length
     << " bytes";
  if (draw.index_payload_truncated) {
    os << " truncated";
  }
  return os.str();
}

std::string VertexPayloadStatus(const VertexFetchRecord &fetch) {
  std::ostringstream os;
  if (fetch.payload_missing || fetch.payload_bytes.empty()) {
    os << "payload=missing";
    return os.str();
  }
  os << "payload=" << fetch.payload_bytes.size() << "/" << fetch.size_bytes
     << " bytes";
  if (fetch.payload_loaded_from_resource) {
    os << " sidecar";
  }
  if (fetch.payload_truncated) {
    os << " truncated";
  }
  return os.str();
}

uint16_t LoadLittleEndian16(const std::vector<uint8_t> &bytes,
                            std::size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) |
         static_cast<uint16_t>(bytes[offset + 1] << 8);
}

uint32_t LoadLittleEndian32(const std::vector<uint8_t> &bytes,
                            std::size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

uint32_t NormalizeIndexEndian(uint32_t index_format, uint32_t endian) {
  if (index_format == 0) {
    if (endian == 2) {
      return 1;
    }
    if (endian == 3) {
      return 0;
    }
  }
  return endian;
}

uint32_t GpuSwapIndex(uint32_t value, uint32_t index_format, uint32_t endian) {
  endian = NormalizeIndexEndian(index_format, endian);
  if (index_format == 0) {
    value &= 0xFFFF;
    if (endian == 1) {
      value = ((value << 8) & 0xFF00) | ((value >> 8) & 0x00FF);
    }
    return value & 0xFFFFFF;
  }

  switch (endian) {
  case 1:
    value = ((value << 8) & 0xFF00FF00) |
            ((value >> 8) & 0x00FF00FF);
    break;
  case 2:
    value = ((value & 0x000000FF) << 24) |
            ((value & 0x0000FF00) << 8) |
            ((value & 0x00FF0000) >> 8) |
            ((value & 0xFF000000) >> 24);
    break;
  case 3:
    value = ((value >> 16) & 0x0000FFFF) | (value << 16);
    break;
  default:
    break;
  }
  return value & 0xFFFFFF;
}

std::vector<uint32_t> DecodeIndexPayload(const PM4DrawRecord &draw) {
  std::vector<uint32_t> indices;
  if (!draw.indexed || draw.index_payload_missing || draw.index_bytes.empty()) {
    return indices;
  }

  const uint32_t index_size = draw.index_format == 0 ? 2 : 4;
  const std::size_t available_count = draw.index_bytes.size() / index_size;
  const std::size_t decode_count =
      std::min<std::size_t>(draw.index_count, available_count);
  indices.reserve(decode_count);
  for (std::size_t i = 0; i < decode_count; ++i) {
    const std::size_t offset = i * index_size;
    uint32_t value = index_size == 2
                         ? LoadLittleEndian16(draw.index_bytes, offset)
                         : LoadLittleEndian32(draw.index_bytes, offset);
    indices.push_back(
        GpuSwapIndex(value, draw.index_format, draw.index_endianness));
  }
  return indices;
}

const char *VertexFormatName(uint32_t format) {
  switch (format) {
  case 6:
    return "FMT_8_8_8_8";
  case 7:
    return "FMT_2_10_10_10";
  case 16:
    return "FMT_10_11_11";
  case 17:
    return "FMT_11_11_10";
  case 25:
    return "FMT_16_16";
  case 26:
    return "FMT_16_16_16_16";
  case 31:
    return "FMT_16_16_FLOAT";
  case 32:
    return "FMT_16_16_16_16_FLOAT";
  case 33:
    return "FMT_32";
  case 34:
    return "FMT_32_32";
  case 35:
    return "FMT_32_32_32_32";
  case 36:
    return "FMT_32_FLOAT";
  case 37:
    return "FMT_32_32_FLOAT";
  case 38:
    return "FMT_32_32_32_32_FLOAT";
  case 57:
    return "FMT_32_32_32_FLOAT";
  default:
    return "FMT_UNKNOWN";
  }
}

uint32_t VertexFormatComponentCount(uint32_t format) {
  switch (format) {
  case 33:
  case 36:
    return 1;
  case 25:
  case 31:
  case 34:
  case 37:
    return 2;
  case 16:
  case 17:
  case 57:
    return 3;
  case 6:
  case 7:
  case 26:
  case 32:
  case 35:
  case 38:
    return 4;
  default:
    return 0;
  }
}

uint32_t VertexFormatByteSize(uint32_t format) {
  switch (format) {
  case 6:
  case 7:
  case 16:
  case 17:
  case 25:
  case 31:
  case 33:
  case 36:
    return 4;
  case 26:
  case 32:
  case 34:
  case 37:
    return 8;
  case 57:
    return 12;
  case 35:
  case 38:
    return 16;
  default:
    return 0;
  }
}

uint32_t GpuSwap32(uint32_t value, uint32_t endian) {
  switch (endian) {
  case 1:
    return ((value << 8) & 0xFF00FF00) |
           ((value >> 8) & 0x00FF00FF);
  case 2:
    return ((value & 0x000000FF) << 24) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0xFF000000) >> 24);
  case 3:
    return ((value >> 16) & 0x0000FFFF) | (value << 16);
  default:
    return value;
  }
}

uint32_t AlignUpU32(uint32_t value, uint32_t alignment) {
  return alignment == 0 ? value
                        : ((value + alignment - 1) / alignment) * alignment;
}

uint32_t XenosTiledOffset2D(uint32_t x, uint32_t y, uint32_t pitch,
                            uint32_t bytes_per_block_log2) {
  pitch = AlignUpU32(pitch, 32);
  const uint32_t macro =
      ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bytes_per_block_log2 + 7);
  const uint32_t micro =
      ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  const uint32_t offset =
      macro + ((micro & ~0xFu) << 1) + (micro & 0xFu) + ((y & 1) << 4);
  return ((offset & ~0x1FFu) << 3) + ((y & 16) << 7) +
         ((offset & 0x1C0u) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3Fu);
}

uint32_t XenosTiledAddressUpperBound2D(uint32_t right, uint32_t bottom,
                                       uint32_t pitch,
                                       uint32_t bytes_per_block_log2) {
  if (right == 0 || bottom == 0) {
    return 0;
  }

  uint32_t upper_bound = XenosTiledOffset2D((right - 1) & ~31u,
                                            (bottom - 1) & ~31u, pitch,
                                            bytes_per_block_log2);
  switch (bytes_per_block_log2) {
  case 0:
    upper_bound += 0xA00;
    break;
  case 1:
    upper_bound += 0xC00;
    break;
  default:
    upper_bound += 0x400u << bytes_per_block_log2;
    break;
  }
  return upper_bound;
}

uint8_t ApplyTextureSwizzleComponent(const std::array<uint8_t, 4> &rgba,
                                     uint32_t swizzle,
                                     uint32_t component_index) {
  const uint32_t component = (swizzle >> (3 * component_index)) & 0b111;
  switch (component) {
  case 0:
  case 1:
  case 2:
  case 3:
    return rgba[component];
  case 4:
    return 0;
  case 5:
    return 0xFF;
  default:
    return 0;
  }
}

bool DecodeFrontbufferFetchRgba8(const PM4SwapRecord &swap,
                                 std::vector<uint8_t> &rgba,
                                 std::string &reason) {
  rgba.clear();
  if (!swap.frontbuffer_fetch_valid) {
    reason = "frontbuffer fetch0 metadata is missing";
    return false;
  }
  const TextureFetchRecord &fetch = swap.frontbuffer_fetch;
  if (fetch.format != 6) {
    reason = "unsupported frontbuffer format " + std::to_string(fetch.format);
    return false;
  }
  if (fetch.width == 0 || fetch.height == 0) {
    reason = "frontbuffer fetch dimensions are zero";
    return false;
  }
  if (fetch.width != swap.width || fetch.height != swap.height) {
    reason = "frontbuffer fetch dimensions do not match PM4 swap dimensions";
    return false;
  }
  const uint64_t output_bytes =
      static_cast<uint64_t>(fetch.width) * fetch.height * 4u;
  if (output_bytes > SIZE_MAX) {
    reason = "decoded frontbuffer output would exceed addressable memory";
    return false;
  }

  const uint32_t pitch_texels =
      fetch.pitch != 0 ? fetch.pitch << 5 : fetch.width;
  if (fetch.tiled) {
    const uint32_t tiled_footprint =
        XenosTiledAddressUpperBound2D(fetch.width, fetch.height, pitch_texels,
                                      2);
    if (tiled_footprint > swap.frontbuffer_payload_bytes.size()) {
      reason = "tiled frontbuffer payload is smaller than fetch footprint "
               "(have " +
               std::to_string(swap.frontbuffer_payload_bytes.size()) +
               " bytes, need " + std::to_string(tiled_footprint) + " bytes)";
      return false;
    }
  } else {
    const uint64_t footprint =
        (static_cast<uint64_t>(pitch_texels) * (fetch.height - 1) +
         fetch.width) *
        4u;
    if (footprint > swap.frontbuffer_payload_bytes.size()) {
      reason = "linear frontbuffer payload is smaller than fetch footprint";
      return false;
    }
  }

  rgba.resize(static_cast<std::size_t>(output_bytes));
  for (uint32_t y = 0; y < fetch.height; ++y) {
    for (uint32_t x = 0; x < fetch.width; ++x) {
      const std::size_t pixel =
          static_cast<std::size_t>(y) * fetch.width + x;
      const std::size_t source_offset =
          fetch.tiled
              ? XenosTiledOffset2D(x, y, pitch_texels, 2)
              : (static_cast<std::size_t>(y) * pitch_texels + x) * 4u;
      if (source_offset + 4 > swap.frontbuffer_payload_bytes.size()) {
        reason = "tiled frontbuffer payload is smaller than fetch footprint";
        rgba.clear();
        return false;
      }
      const uint32_t word = GpuSwap32(
          LoadLittleEndian32(swap.frontbuffer_payload_bytes, source_offset),
          fetch.endian);
      const std::array<uint8_t, 4> raw = {
          static_cast<uint8_t>(word & 0xFF),
          static_cast<uint8_t>((word >> 8) & 0xFF),
          static_cast<uint8_t>((word >> 16) & 0xFF),
          static_cast<uint8_t>((word >> 24) & 0xFF)};
      rgba[pixel * 4 + 0] =
          ApplyTextureSwizzleComponent(raw, fetch.swizzle, 0);
      rgba[pixel * 4 + 1] =
          ApplyTextureSwizzleComponent(raw, fetch.swizzle, 1);
      rgba[pixel * 4 + 2] =
          ApplyTextureSwizzleComponent(raw, fetch.swizzle, 2);
      rgba[pixel * 4 + 3] =
          ApplyTextureSwizzleComponent(raw, fetch.swizzle, 3);
    }
  }
  reason.clear();
  return true;
}

float FloatFromBits(uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

float HalfToFloat(uint16_t bits) {
  const uint32_t sign = uint32_t(bits & 0x8000) << 16;
  uint32_t exponent = (bits >> 10) & 0x1F;
  uint32_t mantissa = bits & 0x03FF;
  uint32_t out = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      out = sign;
    } else {
      exponent = 127 - 15 + 1;
      while ((mantissa & 0x0400) == 0) {
        mantissa <<= 1;
        --exponent;
      }
      mantissa &= 0x03FF;
      out = sign | (exponent << 23) | (mantissa << 13);
    }
  } else if (exponent == 0x1F) {
    out = sign | 0x7F800000 | (mantissa << 13);
  } else {
    out = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
  }
  return FloatFromBits(out);
}

int32_t SignExtend(uint32_t value, uint32_t bit_count) {
  const uint32_t shift = 32 - bit_count;
  return static_cast<int32_t>(value << shift) >> shift;
}

float ConvertPackedComponent(uint32_t raw, uint32_t width,
                             const VertexAttributeRecord &attribute) {
  if (attribute.is_signed) {
    const int32_t signed_value = SignExtend(raw, width);
    if (attribute.is_integer) {
      return static_cast<float>(signed_value);
    }
    const float scale =
        attribute.signed_rf_mode == 1
            ? (1.0f / (float((uint64_t{1} << (width - 1)) - 1) + 0.5f))
            : (1.0f / float((uint64_t{1} << (width - 1)) - 1));
    return static_cast<float>(signed_value) * scale;
  }
  if (attribute.is_integer) {
    return static_cast<float>(raw);
  }
  return static_cast<float>(raw) / float((uint64_t{1} << width) - 1);
}

bool DecodeVertexAttribute(const VertexFetchRecord &fetch,
                           const VertexAttributeRecord &attribute,
                           uint32_t vertex_index,
                           std::vector<float> &components,
                           std::string &format_note) {
  components.clear();
  const uint32_t component_count =
      VertexFormatComponentCount(attribute.data_format);
  const uint32_t byte_size = VertexFormatByteSize(attribute.data_format);
  if (component_count == 0 || byte_size == 0) {
    format_note = "unsupported_format";
    return false;
  }
  const uint64_t base = uint64_t(vertex_index) * fetch.stride_bytes +
                        attribute.offset_bytes;
  if (base + byte_size > fetch.payload_bytes.size()) {
    format_note = "out_of_snapshot";
    return false;
  }

  auto load_word = [&](uint32_t word_index) {
    return GpuSwap32(LoadLittleEndian32(fetch.payload_bytes,
                                        std::size_t(base) + word_index * 4),
                     fetch.endian);
  };

  switch (attribute.data_format) {
  case 6: {
    if (fetch.endian == 2) {
      const uint8_t *bytes = fetch.payload_bytes.data() + std::size_t(base);
      components.push_back(ConvertPackedComponent(bytes[1], 8, attribute));
      components.push_back(ConvertPackedComponent(bytes[2], 8, attribute));
      components.push_back(ConvertPackedComponent(bytes[3], 8, attribute));
      components.push_back(ConvertPackedComponent(bytes[0], 8, attribute));
      return true;
    }
    const uint32_t word = load_word(0);
    for (uint32_t i = 0; i < 4; ++i) {
      components.push_back(ConvertPackedComponent((word >> (i * 8)) & 0xFF,
                                                  8, attribute));
    }
    return true;
  }
  case 7: {
    const uint32_t word = load_word(0);
    components.push_back(ConvertPackedComponent(word & 0x3FF, 10, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 10) & 0x3FF, 10, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 20) & 0x3FF, 10, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 30) & 0x003, 2, attribute));
    return true;
  }
  case 16: {
    const uint32_t word = load_word(0);
    components.push_back(ConvertPackedComponent(word & 0x7FF, 11, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 11) & 0x7FF, 11, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 22) & 0x3FF, 10, attribute));
    return true;
  }
  case 17: {
    const uint32_t word = load_word(0);
    components.push_back(ConvertPackedComponent(word & 0x3FF, 10, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 10) & 0x7FF, 11, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 21) & 0x7FF, 11, attribute));
    return true;
  }
  case 25:
  case 26: {
    for (uint32_t i = 0; i < component_count; ++i) {
      const uint32_t word = load_word(i / 2);
      const uint32_t raw = (word >> ((i & 1) * 16)) & 0xFFFF;
      components.push_back(ConvertPackedComponent(raw, 16, attribute));
    }
    return true;
  }
  case 31:
  case 32: {
    for (uint32_t i = 0; i < component_count; ++i) {
      const uint32_t word = load_word(i / 2);
      const uint32_t raw = (word >> ((i & 1) * 16)) & 0xFFFF;
      components.push_back(HalfToFloat(static_cast<uint16_t>(raw)));
    }
    return true;
  }
  case 33:
  case 34:
  case 35: {
    for (uint32_t i = 0; i < component_count; ++i) {
      const uint32_t word = load_word(i);
      if (attribute.is_signed) {
        const int32_t value = static_cast<int32_t>(word);
        components.push_back(attribute.is_integer
                                 ? static_cast<float>(value)
                                 : static_cast<float>(value) *
                                       (1.0f / 2147483647.0f));
      } else {
        components.push_back(attribute.is_integer
                                 ? static_cast<float>(word)
                                 : static_cast<float>(word) *
                                       (1.0f / 4294967295.0f));
      }
    }
    return true;
  }
  case 36:
  case 37:
  case 38:
  case 57:
    for (uint32_t i = 0; i < component_count; ++i) {
      components.push_back(FloatFromBits(load_word(i)));
    }
    return true;
  default:
    format_note = "unsupported_format";
    return false;
  }
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

bool ParseShaderPairArgument(std::string_view text, uint64_t &vs,
                             uint64_t &ps) {
  const std::size_t separator = text.find(':');
  if (separator == std::string_view::npos || separator == 0 ||
      separator + 1 >= text.size()) {
    return false;
  }
  return ParseIntegerText(text.substr(0, separator), vs) &&
         ParseIntegerText(text.substr(separator + 1), ps);
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
      << "  --dump-frontbuffer     Export a PM4 swap frontbuffer BMP preview\n"
      << "  --frontbuffer-index <index>\n"
         "                          Select PM4 swap payload for --dump-frontbuffer\n"
      << "  --frontbuffer-output <path>\n"
         "                          BMP path for --dump-frontbuffer\n"
      << "  --dump-texture         Export decoded bound texture BMP for --draw\n"
      << "  --texture-slot <index> Select bound texture slot for --dump-texture\n"
      << "  --texture-output <path>\n"
         "                          BMP path for --dump-texture\n"
      << "  --dump-render-target   Export captured color target BMP for --draw\n"
      << "  --render-target-index <index>\n"
         "                          Select color target slot for --dump-render-target\n"
      << "  --render-target-output <path>\n"
         "                          BMP path for --dump-render-target\n"
      << "  --resource-summary     Summarize replay resource snapshot "
         "coverage\n"
      << "  --max-draws <count>    Limit draw dump rows (default 64)\n"
      << "  --shader-usage         Print shader and shader-pair usage\n"
      << "  --shader-pair <vs:ps>  Filter D3D12 real replay to one runtime "
         "shader pair\n"
      << "  --top-shaders <count>  Limit shader usage rows (default 20)\n"
      << "  --missing-shaders      Report draws missing runtime shader hashes\n"
      << "  --real-backend-gaps    Rank blockers for D3D12 real replay\n"
      << "  --shader-record-probes Dump captured XEX shader/material probe events\n"
      << "  --backend <name>       Replay backend selector: "
         "null/d3d12-diagnostic/d3d12/vulkan-diagnostic/vulkan\n"
      << "  --d3d12-output <path>  BMP output for D3D12 replay backends\n"
      << "  --d3d12-depth-output <path>\n"
         "                          Depth/stencil preview BMP for D3D12 real "
         "replay (default: <color-output-stem>-depth.bmp)\n"
      << "  --shader-override-root <path>\n"
         "                          Root containing native shader overrides "
         "(default shader_work/native_overrides)\n"
      << "  --shader-cache-root <path>\n"
         "                          Root for compiled native shader cache "
         "(default shader_work/cache)\n"
      << "  --d3d12-draws <count>  Replay draw tiles to render (default 4096)\n"
      << "  --allow-diagnostic-shader\n"
         "                          Permit --backend d3d12 to use the "
         "temporary diagnostic shader fallback\n"
      << "  --strict               Fail frame replay on the first unsupported "
         "draw (default for --frame --backend d3d12)\n"
      << "  --skip-unsupported     In frame replay, skip unsupported draws and "
         "print grouped reasons\n"
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
  case CaptureEventType::ShaderRecordProbe:
    return "shader_record_probe";
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
  if (type == "shader_record_probe") {
    return CaptureEventType::ShaderRecordProbe;
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

  LoadVertexPayloadSidecars(capture, options);
  LoadTexturePayloadSidecars(capture, options);
  LoadFloatConstantSidecars(capture, options);
  LoadFrontbufferPayloadSidecars(capture, options);
  LoadRenderTargetPayloadSidecars(capture, options);
  AnalyzeReplayCapture(capture, options);
  if (!options.keep_events) {
    capture.events.clear();
  }
  return capture.summary.parse_errors == 0;
}

std::string FormatHex32(uint32_t value) { return FormatHex(value, 8); }

std::string FormatHex64(uint64_t value) { return FormatHex(value, 16); }

bool LoadShaderOverrideManifest(const std::filesystem::path &path,
                                std::vector<ShaderOverrideRecord> &records,
                                std::string &error) {
  records.clear();
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "could not open shader override manifest " + path.string();
    return false;
  }

  file.seekg(0, std::ios::end);
  const std::ifstream::pos_type size = file.tellg();
  if (size == std::ifstream::pos_type(-1)) {
    error = "could not size shader override manifest " + path.string();
    return false;
  }

  std::string text;
  text.resize(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!text.empty()) {
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
      error = "could not read shader override manifest " + path.string();
      return false;
    }
  }

  JsonObject root;
  JsonLineParser parser(text);
  if (!parser.ParseObject(root, error)) {
    error = "could not parse shader override manifest " + path.string() +
            ": " + error;
    return false;
  }

  const JsonValue *overrides = FindValue(root, "overrides");
  if (!overrides || overrides->type != JsonValueType::Array) {
    error = "shader override manifest has no overrides array: " +
            path.string();
    return false;
  }

  for (const JsonValue &element : overrides->array_value) {
    if (element.type != JsonValueType::Object) {
      continue;
    }

    const auto &object = element.object_value;
    ShaderOverrideRecord record;
    record.backend = GetString(object, "backend");
    record.stage = GetString(object, "stage");
    record.runtime_hash = GetU64(object, "runtime_hash");
    record.entry = GetString(object, "entry");
    record.profile = GetString(object, "profile");
    record.path = GetString(object, "path");
    record.source = GetString(object, "source");
    if (!record.backend.empty() && !record.stage.empty() &&
        record.runtime_hash != 0 && !record.path.empty()) {
      records.push_back(std::move(record));
    }
  }

  return true;
}

bool LoadShaderCacheIndex(const std::filesystem::path &path,
                          std::vector<ShaderCacheRecord> &records,
                          std::string &error) {
  records.clear();
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "could not open shader cache index " + path.string();
    return false;
  }

  file.seekg(0, std::ios::end);
  const std::ifstream::pos_type size = file.tellg();
  if (size == std::ifstream::pos_type(-1)) {
    error = "could not size shader cache index " + path.string();
    return false;
  }

  std::string text;
  text.resize(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!text.empty()) {
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
      error = "could not read shader cache index " + path.string();
      return false;
    }
  }

  auto append_record = [&records](const auto &object) {
    ShaderCacheRecord record;
    record.backend = GetString(object, "backend");
    record.stage = GetString(object, "stage");
    record.runtime_hash = GetU64(object, "runtime_hash");
    record.entry = GetString(object, "entry");
    record.profile = GetString(object, "profile");
    record.compiler = GetString(object, "compiler");
    record.format = GetString(object, "format");
    record.cache_key = GetString(object, "cache_key");
    record.path = GetString(object, "path");
    record.source = GetString(object, "source");
    record.diagnostic = GetBool(object, "diagnostic");
    if (!record.backend.empty() && !record.stage.empty() &&
        record.runtime_hash != 0 && !record.path.empty()) {
      records.push_back(std::move(record));
    }
  };

  JsonObject root;
  JsonLineParser parser(text);
  std::string object_error;
  if (parser.ParseObject(root, object_error)) {
    const JsonValue *records_value = FindValue(root, "records");
    if (!records_value || records_value->type != JsonValueType::Array) {
      error = "shader cache index has no records array: " + path.string();
      return false;
    }

    for (const JsonValue &element : records_value->array_value) {
      if (element.type != JsonValueType::Object) {
        continue;
      }
      append_record(element.object_value);
    }
    return true;
  }

  std::istringstream lines(text);
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(lines, line)) {
    ++line_number;
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      continue;
    }
    const auto last = line.find_last_not_of(" \t\r\n");
    const std::string trimmed = line.substr(first, last - first + 1);

    JsonObject line_object;
    JsonLineParser line_parser(trimmed);
    std::string line_error;
    if (!line_parser.ParseObject(line_object, line_error)) {
      error = "could not parse shader cache JSONL " + path.string() +
              " at line " + std::to_string(line_number) + ": " + line_error;
      return false;
    }
    append_record(line_object);
  }
  return true;
}

std::vector<std::string> ExtractAsciiRunsFromDwords(
    const std::vector<uint32_t> &dwords) {
  std::vector<std::string> runs;
  std::string current;
  auto flush = [&]() {
    if (current.size() >= 8) {
      runs.push_back(current);
    }
    current.clear();
  };

  for (const uint32_t dword : dwords) {
    for (int shift = 24; shift >= 0; shift -= 8) {
      const char ch = static_cast<char>((dword >> shift) & 0xFF);
      if (ch >= 0x20 && ch <= 0x7E) {
        current.push_back(ch);
      } else {
        flush();
      }
    }
  }
  flush();
  return runs;
}

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
  std::cout << "Shader payloads: with_payload="
            << capture.summary.shader_uploads_with_payload
            << " missing_payload="
            << capture.summary.shader_uploads_missing_payload
            << " payload_dwords=" << capture.summary.shader_payload_dwords
            << " truncated=" << capture.summary.shader_payload_truncated
            << "\n";
  std::cout << "Constants: with_payload="
            << capture.summary.constant_uploads_with_payload
            << " missing_payload="
            << capture.summary.constant_uploads_missing_payload
            << " payload_dwords=" << capture.summary.constant_payload_dwords
            << " truncated=" << capture.summary.constant_payload_truncated
            << "\n";
  std::cout << "Index buffers: snapshots="
            << capture.summary.index_buffer_snapshots
            << " missing=" << capture.summary.index_buffer_snapshots_missing
            << " payload_bytes=" << capture.summary.index_payload_bytes
            << " truncated=" << capture.summary.index_payload_truncated
            << "\n";
  std::cout << "Vertex fetch: draws_with="
            << capture.summary.draws_with_vertex_fetch
            << " draws_no_fetch="
            << capture.summary.draws_missing_vertex_fetch
            << " state_missing="
            << capture.summary.draws_missing_vertex_fetch_state
            << " records=" << capture.summary.vertex_fetch_records
            << " snapshots=" << capture.summary.vertex_buffer_snapshots
            << " missing="
            << capture.summary.vertex_buffer_snapshots_missing
            << " payload_bytes=" << capture.summary.vertex_payload_bytes
            << " truncated=" << capture.summary.vertex_payload_truncated
            << "\n";
  std::cout << "Texture fetch: draws_with="
            << capture.summary.draws_with_texture_fetch
            << " draws_no_fetch="
            << capture.summary.draws_missing_texture_fetch
            << " state_missing="
            << capture.summary.draws_missing_texture_fetch_state
            << " records=" << capture.summary.texture_fetch_records
            << " clamp_modes="
            << capture.summary.texture_fetches_with_clamp_modes << "/"
            << capture.summary.texture_fetch_records
            << " snapshots=" << capture.summary.texture_snapshots
            << " missing=" << capture.summary.texture_snapshots_missing
            << " payload_bytes=" << capture.summary.texture_payload_bytes
            << " sidecars=" << capture.summary.texture_payload_sidecars
            << " sidecar_bytes="
            << capture.summary.texture_payload_sidecar_bytes
            << " truncated=" << capture.summary.texture_payload_truncated
            << "\n";
  std::cout << "Render state: draws_with="
            << capture.summary.draws_with_render_state
            << " missing=" << capture.summary.draws_missing_render_state
            << "\n";
  std::cout << "Render targets: color_snapshots="
            << capture.summary.color_target_snapshots
            << " color_missing="
            << capture.summary.color_target_snapshots_missing
            << " color_payload_bytes="
            << capture.summary.color_target_payload_bytes
            << " depth_snapshots="
            << capture.summary.depth_target_snapshots
            << " depth_missing="
            << capture.summary.depth_target_snapshots_missing
            << " depth_payload_bytes="
            << capture.summary.depth_target_payload_bytes << "\n";
  std::cout << "Frontbuffer snapshots: snapshots="
            << capture.summary.frontbuffer_snapshots
            << " missing="
            << capture.summary.frontbuffer_snapshots_missing
            << " payload_bytes="
            << capture.summary.frontbuffer_payload_bytes
            << " sidecars="
            << capture.summary.frontbuffer_payload_sidecars
            << " truncated="
            << capture.summary.frontbuffer_payload_truncated << "\n";

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
              << " endian=" << draw.index_endianness << " "
              << IndexPayloadStatus(draw) << "\n";
    std::cout << "    VS=" << FormatHex64(state.vertex_shader.hash)
              << " PS=" << FormatHex64(state.pixel_shader.hash)
              << " constants_total=" << state.constants_seen_total
              << " constants_frame=" << state.constants_seen_in_frame
              << " last_constant_seq=" << state.last_constant_seq
              << " vertex_fetches=" << draw.vertex_fetches.size() << "/"
              << draw.vertex_fetch_count
              << " texture_fetches=" << draw.texture_fetches.size() << "/"
              << draw.texture_fetch_count
              << " render_state="
              << (draw.render_state.present ? "yes" : "no")
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

struct DecodedBlendControl {
  uint32_t src_blend = 0;
  uint32_t blend_op = 0;
  uint32_t dest_blend = 0;
  uint32_t alpha_src_blend = 0;
  uint32_t alpha_blend_op = 0;
  uint32_t alpha_dest_blend = 0;
  bool blend_enable = false;
};

DecodedBlendControl DecodeBlendControl(uint32_t blend_control) {
  DecodedBlendControl decoded{};
  decoded.src_blend = (blend_control >> 0) & 0x1F;
  decoded.blend_op = (blend_control >> 5) & 0x7;
  decoded.dest_blend = (blend_control >> 8) & 0x1F;
  decoded.alpha_src_blend = (blend_control >> 16) & 0x1F;
  decoded.alpha_blend_op = (blend_control >> 21) & 0x7;
  decoded.alpha_dest_blend = (blend_control >> 24) & 0x1F;
  decoded.blend_enable =
      !(decoded.src_blend == 1 && decoded.dest_blend == 0 &&
        decoded.blend_op == 0 && decoded.alpha_src_blend == 1 &&
        decoded.alpha_dest_blend == 0 && decoded.alpha_blend_op == 0);
  return decoded;
}

const char *XenosBlendFactorName(uint32_t factor, bool alpha) {
  switch (factor & 0x1F) {
  case 0:
    return "ZERO";
  case 1:
    return "ONE";
  case 4:
    return alpha ? "SRC_ALPHA" : "SRC_COLOR";
  case 5:
    return alpha ? "INV_SRC_ALPHA" : "INV_SRC_COLOR";
  case 6:
    return "SRC_ALPHA";
  case 7:
    return "INV_SRC_ALPHA";
  case 8:
    return alpha ? "DEST_ALPHA" : "DEST_COLOR";
  case 9:
    return alpha ? "INV_DEST_ALPHA" : "INV_DEST_COLOR";
  case 10:
    return "DEST_ALPHA";
  case 11:
    return "INV_DEST_ALPHA";
  case 12:
    return "BLEND_FACTOR";
  case 13:
    return "INV_BLEND_FACTOR";
  case 14:
    return "CONST_ALPHA";
  case 15:
    return "INV_CONST_ALPHA";
  case 16:
    return "SRC_ALPHA_SAT";
  default:
    return "UNKNOWN";
  }
}

const char *XenosBlendOpName(uint32_t op) {
  switch (op & 0x7) {
  case 0:
    return "ADD";
  case 1:
    return "SUBTRACT";
  case 2:
    return "MIN";
  case 3:
    return "MAX";
  case 4:
    return "REV_SUBTRACT";
  default:
    return "UNKNOWN";
  }
}

void PrintBlendControlDecode(uint32_t blend_control, const char *indent) {
  const DecodedBlendControl decoded = DecodeBlendControl(blend_control);
  std::cout << indent << "rb_blendcontrol[0]="
            << FormatHex32(blend_control)
            << " enable=" << (decoded.blend_enable ? "yes" : "no")
            << " color=(" << XenosBlendFactorName(decoded.src_blend, false)
            << " " << XenosBlendOpName(decoded.blend_op) << " "
            << XenosBlendFactorName(decoded.dest_blend, false) << ")"
            << " alpha=("
            << XenosBlendFactorName(decoded.alpha_src_blend, true) << " "
            << XenosBlendOpName(decoded.alpha_blend_op) << " "
            << XenosBlendFactorName(decoded.alpha_dest_blend, true) << ")"
            << " raw_fields=" << decoded.src_blend << ","
            << decoded.blend_op << "," << decoded.dest_blend << ","
            << decoded.alpha_src_blend << "," << decoded.alpha_blend_op
            << "," << decoded.alpha_dest_blend << "\n";
}

void PrintScissorDecode(const RenderStateRecord &rs, const char *indent) {
  const uint32_t screen_tl_x = rs.pa_sc_screen_scissor_tl & 0x7FFFu;
  const uint32_t screen_tl_y = (rs.pa_sc_screen_scissor_tl >> 16) & 0x7FFFu;
  const uint32_t screen_br_x = rs.pa_sc_screen_scissor_br & 0x7FFFu;
  const uint32_t screen_br_y = (rs.pa_sc_screen_scissor_br >> 16) & 0x7FFFu;
  const uint32_t window_tl_x = rs.pa_sc_window_scissor_tl & 0x7FFFu;
  const uint32_t window_tl_y = (rs.pa_sc_window_scissor_tl >> 16) & 0x7FFFu;
  const uint32_t window_br_x = rs.pa_sc_window_scissor_br & 0x7FFFu;
  const uint32_t window_br_y = (rs.pa_sc_window_scissor_br >> 16) & 0x7FFFu;
  std::cout << indent << "screen_scissor=" << screen_tl_x << ","
            << screen_tl_y << " -> " << screen_br_x << "," << screen_br_y
            << " raw=(" << FormatHex32(rs.pa_sc_screen_scissor_tl) << ","
            << FormatHex32(rs.pa_sc_screen_scissor_br) << ")\n";
  std::cout << indent << "window_scissor=" << window_tl_x << ","
            << window_tl_y << " -> " << window_br_x << "," << window_br_y
            << " raw=(" << FormatHex32(rs.pa_sc_window_scissor_tl) << ","
            << FormatHex32(rs.pa_sc_window_scissor_br)
            << ") offset=" << FormatHex32(rs.pa_sc_window_offset) << "\n";
}

void PrintRenderStateDecode(const RenderStateRecord &rs) {
  std::cout << "    raw_controls rb_colorcontrol="
            << FormatHex32(rs.rb_colorcontrol)
            << " rb_modecontrol=" << FormatHex32(rs.rb_modecontrol)
            << " rb_surface_info=" << FormatHex32(rs.rb_surface_info)
            << " rb_depthcontrol=" << FormatHex32(rs.rb_depthcontrol)
            << " rb_alpha_ref=" << FormatHex32(rs.rb_alpha_ref) << "\n";
  std::cout << "    raster pa_su_sc_mode_cntl="
            << FormatHex32(rs.pa_su_sc_mode_cntl)
            << " pa_su_vtx_cntl=" << FormatHex32(rs.pa_su_vtx_cntl)
            << " pa_cl_clip_cntl=" << FormatHex32(rs.pa_cl_clip_cntl)
            << " pa_cl_vte_cntl=" << FormatHex32(rs.pa_cl_vte_cntl)
            << " cull=" << rs.cull_mode << " fill=" << rs.fill_mode
            << " front_face=" << rs.front_face << "\n";
  std::cout << "    shader_control sq_program_cntl="
            << FormatHex32(rs.sq_program_cntl)
            << " sq_context_misc=" << FormatHex32(rs.sq_context_misc)
            << " param_gen="
            << (((rs.sq_program_cntl >> 18) & 0x1u) ? "yes" : "no")
            << " param_gen_pos=" << ((rs.sq_context_misc >> 8) & 0xFFu)
            << "\n";
  if (!rs.rb_blendcontrol.empty()) {
    PrintBlendControlDecode(rs.rb_blendcontrol[0], "    ");
  } else {
    std::cout << "    rb_blendcontrol[0]: missing\n";
  }
  PrintScissorDecode(rs, "    ");
  if (!rs.viewport_registers.empty()) {
    std::cout << "    viewport_registers:";
    for (uint32_t value : rs.viewport_registers) {
      std::cout << " " << FormatHex32(value);
    }
    std::cout << "\n";
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
            << " " << IndexPayloadStatus(draw)
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
  std::cout << "  vertex_fetches=" << draw.vertex_fetches.size() << "/"
            << draw.vertex_fetch_count
            << " truncated="
            << (draw.vertex_fetch_truncated ? "yes" : "no") << "\n";
  std::cout << "  texture_fetches=" << draw.texture_fetches.size() << "/"
            << draw.texture_fetch_count
            << " truncated="
            << (draw.texture_fetch_truncated ? "yes" : "no") << "\n";
  for (const TextureFetchRecord &fetch : draw.texture_fetches) {
    std::cout << "    tex shader_type=" << fetch.shader_type
              << " binding=" << fetch.binding_index
              << " fetch=" << fetch.fetch_constant
              << " base=" << FormatHex32(fetch.base_address_bytes)
              << " mip=" << FormatHex32(fetch.mip_address_bytes)
              << " size=" << fetch.width << "x" << fetch.height
              << "x" << fetch.depth_or_stack
              << " format=" << fetch.format
              << " dim=" << fetch.dimension
              << " tiled=" << (fetch.tiled ? "yes" : "no")
              << " endian=" << fetch.endian
              << " clamp="
              << (fetch.clamp_modes_present ? "" : "missing:")
              << fetch.clamp_x << "," << fetch.clamp_y << ","
              << fetch.clamp_z << " filter=" << fetch.mag_filter << ","
              << fetch.min_filter << "," << fetch.mip_filter
              << " payload=" << fetch.payload_bytes.size()
              << (fetch.payload_loaded_from_resource ? " sidecar" : "")
              << (fetch.payload_missing ? " missing" : "")
              << (fetch.payload_truncated ? " truncated" : "") << "\n";
  }
  if (draw.render_state.present) {
    const RenderStateRecord &rs = draw.render_state;
    std::cout << "  render_state surface_pitch=" << rs.surface_pitch
              << " msaa=" << rs.msaa_samples
              << " color_mask=" << FormatHex32(rs.rb_color_mask)
              << " depth_base=" << rs.depth_base
              << " depth_format=" << rs.depth_format
              << " depth_test=" << (rs.depth_test_enable ? "yes" : "no")
              << " depth_write=" << (rs.depth_write_enable ? "yes" : "no")
              << " stencil=" << (rs.stencil_enable ? "yes" : "no")
              << " cull=" << rs.cull_mode << "\n";
    PrintRenderStateDecode(rs);
    std::cout << "    color_formats:";
    for (uint32_t format : rs.color_format) {
      std::cout << " " << format;
    }
    std::cout << " bases:";
    for (uint32_t base : rs.color_base) {
      std::cout << " " << base;
    }
    std::cout << "\n";
    if (!rs.color_target_payloads.empty()) {
      std::cout << "    color_target_payloads:";
      for (const RenderTargetPayloadRecord &payload :
           rs.color_target_payloads) {
        std::cout << " [rt=" << payload.target
                  << " base=" << payload.base
                  << " offset=" << payload.payload_offset_bytes
                  << " payload=" << payload.payload_bytes.size() << "/"
                  << payload.payload_requested_byte_count
                  << (payload.payload_loaded_from_resource ? " sidecar" : "")
                  << (payload.payload_missing ? " missing" : "")
                  << (payload.payload_truncated ? " truncated" : "")
                  << "]";
      }
      std::cout << "\n";
    }
    const RenderTargetPayloadRecord &depth_payload = rs.depth_target_payload;
    if (depth_payload.base != 0 ||
        depth_payload.payload_requested_byte_count != 0 ||
        !depth_payload.payload_bytes.empty()) {
      std::cout << "    depth_target_payload base=" << depth_payload.base
                << " offset=" << depth_payload.payload_offset_bytes
                << " payload=" << depth_payload.payload_bytes.size() << "/"
                << depth_payload.payload_requested_byte_count
                << (depth_payload.payload_loaded_from_resource ? " sidecar" : "")
                << (depth_payload.payload_missing ? " missing" : "")
                << (depth_payload.payload_truncated ? " truncated" : "")
                << "\n";
    }
  } else {
    std::cout << "  render_state: missing\n";
  }

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
            << " endian=" << draw.index_endianness
            << " " << IndexPayloadStatus(draw) << "\n";
  if (!draw.indexed) {
    std::cout << "  no explicit index buffer: this draw is auto-indexed or "
                 "source-selected by the GPU packet\n";
    return;
  }
  if (draw.index_payload_missing || draw.index_bytes.empty()) {
    std::cout
        << "  raw index snapshot: missing. Current capture stores packet "
           "metadata only; resource snapshots must be added at the CP/guest "
           "memory boundary before decoded indices can be replayed.\n";
    return;
  }

  std::cout << "  raw index bytes:";
  const std::size_t raw_limit = std::min<std::size_t>(draw.index_bytes.size(), 64);
  for (std::size_t i = 0; i < raw_limit; ++i) {
    std::cout << (i == 0 ? " " : ",")
              << FormatHex(static_cast<uint32_t>(draw.index_bytes[i]), 2);
  }
  if (draw.index_bytes.size() > raw_limit) {
    std::cout << ",...";
  }
  std::cout << "\n";

  const std::vector<uint32_t> decoded_indices = DecodeIndexPayload(draw);
  std::cout << "  decoded_indices=" << decoded_indices.size() << "/"
            << draw.index_count << ":";
  const std::size_t decoded_limit =
      std::min<std::size_t>(decoded_indices.size(), 64);
  for (std::size_t i = 0; i < decoded_limit; ++i) {
    std::cout << (i == 0 ? " " : ",") << decoded_indices[i];
  }
  if (decoded_indices.size() > decoded_limit) {
    std::cout << ",...";
  }
  if (decoded_indices.size() < draw.index_count) {
    std::cout << " (incomplete)";
  }
  std::cout << "\n";
}

void PrintVertexDump(const ReplayCapture &capture, std::size_t draw_index) {
  if (draw_index >= capture.draws.size()) {
    std::cout << "Draw " << draw_index << " does not exist; capture has "
              << capture.draws.size() << " draws\n";
    return;
  }

  const ReplayDrawState &state = capture.draws[draw_index];
  const PM4DrawRecord &draw = state.draw;
  std::cout << "Vertex/fetch dump for draw[" << draw_index
            << "] seq=" << state.seq << "\n";
  std::cout << "  VS=" << FormatHex64(state.vertex_shader.hash)
            << " PS=" << FormatHex64(state.pixel_shader.hash)
            << " bound_constant_ranges=" << state.bound_constants.size()
            << "\n";
  std::cout << "  vertex_fetches=" << draw.vertex_fetches.size() << "/"
            << draw.vertex_fetch_count
            << " truncated="
            << (draw.vertex_fetch_truncated ? "yes" : "no") << "\n";

  if (draw.vertex_fetches.empty()) {
    uint64_t fetch_like_ranges = 0;
    for (const PM4ConstantRecord &constant : state.bound_constants) {
      if (constant.constant_type == 1) {
        ++fetch_like_ranges;
        PrintConstantLine(constant, "  fetch_candidate ");
      }
    }
    if (fetch_like_ranges == 0) {
      std::cout << "  fetch constants: missing. This capture was produced "
                   "before per-draw vertex fetch snapshots were added, or the "
                   "active vertex shader had no decoded vertex bindings.\n";
    }
    return;
  }

  for (const VertexFetchRecord &fetch : draw.vertex_fetches) {
    std::cout << "  vf" << fetch.fetch_constant
              << " raw=(" << FormatHex32(fetch.dword_0) << ","
              << FormatHex32(fetch.dword_1) << ") type=" << fetch.type
              << " addr=" << FormatHex32(fetch.address_bytes)
              << " size=" << fetch.size_bytes
              << " stride=" << fetch.stride_bytes
              << " endian=" << fetch.endian << " "
              << VertexPayloadStatus(fetch) << "\n";
    if (fetch.attributes.empty()) {
      std::cout << "    attributes: none captured\n";
    } else {
      for (std::size_t i = 0; i < fetch.attributes.size(); ++i) {
        const VertexAttributeRecord &attribute = fetch.attributes[i];
        std::cout << "    attr[" << i << "] format="
                  << attribute.data_format << "("
                  << VertexFormatName(attribute.data_format) << ")"
                  << " offset=" << attribute.offset_bytes
                  << " stride=" << attribute.stride_bytes
                  << " bytes=" << VertexFormatByteSize(attribute.data_format)
                  << " exp_adjust=" << attribute.exp_adjust
                  << " prefetch=" << attribute.prefetch_count
                  << " signed=" << (attribute.is_signed ? "yes" : "no")
                  << " integer=" << (attribute.is_integer ? "yes" : "no")
                  << " rounded="
                  << (attribute.is_index_rounded ? "yes" : "no") << "\n";
      }
    }
    const uint32_t vertex_count =
        fetch.stride_bytes == 0
            ? 0
            : static_cast<uint32_t>(
                  fetch.payload_bytes.size() / fetch.stride_bytes);
    if (vertex_count != 0) {
      const uint32_t preview_count = std::min<uint32_t>(vertex_count, 4);
      std::cout << "    decoded vertex preview count=" << preview_count
                << "/" << vertex_count << "\n";
      for (uint32_t vertex_index = 0; vertex_index < preview_count;
           ++vertex_index) {
        std::cout << "      v[" << vertex_index << "]";
        for (std::size_t attr_index = 0; attr_index < fetch.attributes.size();
             ++attr_index) {
          const VertexAttributeRecord &attribute = fetch.attributes[attr_index];
          std::vector<float> components;
          std::string note;
          const bool decoded =
              DecodeVertexAttribute(fetch, attribute, vertex_index, components,
                                    note);
          std::cout << " a" << attr_index << "=";
          if (!decoded) {
            std::cout << note;
            continue;
          }
          std::cout << "(";
          for (std::size_t component_index = 0;
               component_index < components.size(); ++component_index) {
            if (component_index) {
              std::cout << ",";
            }
            std::cout << std::fixed << std::setprecision(6)
                      << components[component_index];
          }
          std::cout.unsetf(std::ios::floatfield);
          std::cout << ")";
        }
        std::cout << "\n";
      }
    }
    if (!fetch.payload_missing && !fetch.payload_bytes.empty()) {
      std::cout << "    raw vertex bytes:";
      const std::size_t raw_limit =
          std::min<std::size_t>(fetch.payload_bytes.size(), 96);
      for (std::size_t i = 0; i < raw_limit; ++i) {
        std::cout << (i == 0 ? " " : ",")
                  << FormatHex(static_cast<uint32_t>(fetch.payload_bytes[i]),
                               2);
      }
      if (fetch.payload_bytes.size() > raw_limit) {
        std::cout << ",...";
      }
      std::cout << "\n";
    }
  }
}

void PrintResourceSummary(const ReplayCapture &capture) {
  const auto shader_count_it =
      capture.summary.event_counts.find(CaptureEventType::PM4Shader);
  const uint64_t shader_uploads =
      shader_count_it == capture.summary.event_counts.end()
          ? 0
          : shader_count_it->second;
  const auto constant_count_it =
      capture.summary.event_counts.find(CaptureEventType::PM4Constants);
  const uint64_t constant_uploads =
      constant_count_it == capture.summary.event_counts.end()
          ? 0
          : constant_count_it->second;
  const auto probe_count_it =
      capture.summary.event_counts.find(CaptureEventType::ShaderRecordProbe);
  const uint64_t shader_record_probes =
      probe_count_it == capture.summary.event_counts.end()
          ? 0
          : probe_count_it->second;
  std::cout << "Resource snapshot summary:\n";
  std::cout << "  shader_uploads=" << shader_uploads
            << " with_payload=" << capture.summary.shader_uploads_with_payload
            << " missing_payload="
            << capture.summary.shader_uploads_missing_payload
            << " payload_dwords=" << capture.summary.shader_payload_dwords
            << " truncated=" << capture.summary.shader_payload_truncated
            << "\n";
  std::cout << "  constant_uploads=" << constant_uploads
            << " with_payload=" << capture.summary.constant_uploads_with_payload
            << " missing_payload="
            << capture.summary.constant_uploads_missing_payload
            << " payload_dwords=" << capture.summary.constant_payload_dwords
            << "\n";
  std::cout << "  index_buffer_snapshots="
            << capture.summary.index_buffer_snapshots
            << " missing=" << capture.summary.index_buffer_snapshots_missing
            << " payload_bytes=" << capture.summary.index_payload_bytes
            << " truncated=" << capture.summary.index_payload_truncated
            << "\n";
  std::cout << "  vertex_fetch_records="
            << capture.summary.vertex_fetch_records
            << " draws_with=" << capture.summary.draws_with_vertex_fetch
            << " draws_no_fetch="
            << capture.summary.draws_missing_vertex_fetch
            << " state_missing="
            << capture.summary.draws_missing_vertex_fetch_state << "\n";
  std::cout << "  vertex_buffer_snapshots="
            << capture.summary.vertex_buffer_snapshots
            << " missing="
            << capture.summary.vertex_buffer_snapshots_missing
            << " payload_bytes=" << capture.summary.vertex_payload_bytes
            << " truncated=" << capture.summary.vertex_payload_truncated
            << "\n";
  std::cout << "  texture_fetch_records="
            << capture.summary.texture_fetch_records
            << " draws_with=" << capture.summary.draws_with_texture_fetch
            << " draws_no_fetch="
            << capture.summary.draws_missing_texture_fetch
            << " state_missing="
            << capture.summary.draws_missing_texture_fetch_state
            << " clamp_modes="
            << capture.summary.texture_fetches_with_clamp_modes << "/"
            << capture.summary.texture_fetch_records
            << " missing_clamp_modes="
            << capture.summary.texture_fetches_missing_clamp_modes << "\n";
  std::cout << "  texture_snapshots=" << capture.summary.texture_snapshots
            << " missing=" << capture.summary.texture_snapshots_missing
            << " payload_bytes=" << capture.summary.texture_payload_bytes
            << " sidecars=" << capture.summary.texture_payload_sidecars
            << " sidecar_bytes="
            << capture.summary.texture_payload_sidecar_bytes
            << " truncated=" << capture.summary.texture_payload_truncated
            << "\n";
  std::cout << "  render_state_draws="
            << capture.summary.draws_with_render_state
            << " missing=" << capture.summary.draws_missing_render_state
            << "\n";
  std::cout << "  shader_record_probes=" << shader_record_probes << "\n";
  std::cout << "  frontbuffer_snapshots="
            << capture.summary.frontbuffer_snapshots
            << " missing="
            << capture.summary.frontbuffer_snapshots_missing
            << " payload_bytes="
            << capture.summary.frontbuffer_payload_bytes
            << " sidecars="
            << capture.summary.frontbuffer_payload_sidecars
            << " sidecar_bytes="
            << capture.summary.frontbuffer_payload_sidecar_bytes
            << " truncated="
            << capture.summary.frontbuffer_payload_truncated << "\n";
  std::cout << "  color_target_snapshots="
            << capture.summary.color_target_snapshots
            << " missing="
            << capture.summary.color_target_snapshots_missing
            << " payload_bytes="
            << capture.summary.color_target_payload_bytes
            << " sidecars="
            << capture.summary.color_target_payload_sidecars
            << " sidecar_bytes="
            << capture.summary.color_target_payload_sidecar_bytes
            << " truncated="
            << capture.summary.color_target_payload_truncated << "\n";
  std::cout << "  depth_target_snapshots="
            << capture.summary.depth_target_snapshots
            << " missing="
            << capture.summary.depth_target_snapshots_missing
            << " payload_bytes="
            << capture.summary.depth_target_payload_bytes
            << " sidecars="
            << capture.summary.depth_target_payload_sidecars
            << " sidecar_bytes="
            << capture.summary.depth_target_payload_sidecar_bytes
            << " truncated="
            << capture.summary.depth_target_payload_truncated << "\n";
  const std::filesystem::path resource_dir =
      capture.path.parent_path() / "resources";
  const bool sidecar_json_present =
      std::filesystem::exists(resource_dir / "index.json");
  const bool sidecar_jsonl_present =
      std::filesystem::exists(resource_dir / "index.jsonl");
  std::cout << "  sidecar_resource_manifest="
            << ((sidecar_json_present || sidecar_jsonl_present) ? "present"
                                                                : "missing")
            << " json=" << (sidecar_json_present ? "yes" : "no")
            << " jsonl=" << (sidecar_jsonl_present ? "yes" : "no") << "\n";
  if (capture.summary.index_buffer_snapshots == 0 ||
      capture.summary.draws_missing_vertex_fetch_state != 0) {
    std::cout
        << "  real backend blocker: replay has draw/shader/constant metadata, "
           "but no complete index/vertex/texture/render-target resource "
           "snapshots yet.\n";
  } else {
    std::cout
        << "  real backend blocker: replay now has bounded index and vertex "
           "snapshots. Fresh captures may also include bounded texture "
           "snapshots and render state, but full render-target/depth snapshots "
           "and automatic native shader translation are still required for "
           "complete scene rendering.\n";
  }
}

void WriteLe16(std::ofstream &file, uint16_t value) {
  file.put(static_cast<char>(value & 0xFF));
  file.put(static_cast<char>((value >> 8) & 0xFF));
}

void WriteLe32(std::ofstream &file, uint32_t value) {
  WriteLe16(file, static_cast<uint16_t>(value & 0xFFFF));
  WriteLe16(file, static_cast<uint16_t>((value >> 16) & 0xFFFF));
}

bool WriteRawLinearRgbaBmpPreview(const std::filesystem::path &path,
                                  const std::vector<uint8_t> &rgba,
                                  uint32_t width, uint32_t height,
                                  std::string &error) {
  if (width == 0 || height == 0) {
    error = "frontbuffer dimensions are zero";
    return false;
  }
  const uint64_t pixel_count = static_cast<uint64_t>(width) * height;
  const uint64_t required_bytes = pixel_count * 4u;
  if (required_bytes > rgba.size()) {
    error = "frontbuffer payload is smaller than width * height * 4";
    return false;
  }
  if (required_bytes > UINT32_MAX) {
    error = "frontbuffer BMP would exceed 4 GiB";
    return false;
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "could not open frontbuffer BMP output: " + path.string();
    return false;
  }

  const uint32_t pixel_bytes = static_cast<uint32_t>(required_bytes);
  constexpr uint32_t file_header_size = 14;
  constexpr uint32_t dib_header_size = 40;
  constexpr uint32_t pixel_offset = file_header_size + dib_header_size;
  const uint32_t file_size = pixel_offset + pixel_bytes;

  file.put('B');
  file.put('M');
  WriteLe32(file, file_size);
  WriteLe16(file, 0);
  WriteLe16(file, 0);
  WriteLe32(file, pixel_offset);

  WriteLe32(file, dib_header_size);
  WriteLe32(file, width);
  WriteLe32(file, static_cast<uint32_t>(-static_cast<int32_t>(height)));
  WriteLe16(file, 1);
  WriteLe16(file, 32);
  WriteLe32(file, 0);
  WriteLe32(file, pixel_bytes);
  WriteLe32(file, 2835);
  WriteLe32(file, 2835);
  WriteLe32(file, 0);
  WriteLe32(file, 0);

  std::array<uint8_t, 4> bgra{};
  for (uint32_t y = 0; y < height; ++y) {
    const std::size_t row_offset = static_cast<std::size_t>(y) * width * 4u;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t *src =
          rgba.data() + row_offset + static_cast<std::size_t>(x) * 4u;
      bgra[0] = src[2];
      bgra[1] = src[1];
      bgra[2] = src[0];
      bgra[3] = 0xFF;
      file.write(reinterpret_cast<const char *>(bgra.data()), bgra.size());
    }
  }

  return true;
}

bool DumpFrontbufferPreview(const ReplayCapture &capture,
                            const ReplayCliOptions &options) {
  const CaptureEvent *selected = nullptr;
  std::size_t selected_index = 0;
  const CaptureEvent *first_snapshot = nullptr;
  std::size_t first_snapshot_index = 0;
  const CaptureEvent *first_nonzero_snapshot = nullptr;
  std::size_t first_nonzero_snapshot_index = 0;
  std::size_t snapshot_index = 0;

  for (const CaptureEvent &event : capture.events) {
    if (event.type != CaptureEventType::PM4Swap || event.swap.width == 0 ||
        event.swap.height == 0 || event.swap.frontbuffer_payload_missing ||
        event.swap.frontbuffer_payload_bytes.empty()) {
      continue;
    }

    if (!first_snapshot) {
      first_snapshot = &event;
      first_snapshot_index = snapshot_index;
    }

    bool any_nonzero = false;
    bool any_rgb_nonzero = false;
    const std::vector<uint8_t> &bytes = event.swap.frontbuffer_payload_bytes;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      if (bytes[i] == 0) {
        continue;
      }
      any_nonzero = true;
      if ((i & 3u) != 3u) {
        any_rgb_nonzero = true;
        break;
      }
    }
    if (any_nonzero && !first_nonzero_snapshot) {
      first_nonzero_snapshot = &event;
      first_nonzero_snapshot_index = snapshot_index;
    }
    if (options.frontbuffer_index) {
      if (snapshot_index == *options.frontbuffer_index) {
        selected = &event;
        selected_index = snapshot_index;
        break;
      }
    } else if (any_rgb_nonzero) {
      selected = &event;
      selected_index = snapshot_index;
      break;
    }
    ++snapshot_index;
  }

  if (!selected && !options.frontbuffer_index && first_nonzero_snapshot) {
    selected = first_nonzero_snapshot;
    selected_index = first_nonzero_snapshot_index;
  }

  if (!selected && !options.frontbuffer_index && first_snapshot) {
    selected = first_snapshot;
    selected_index = first_snapshot_index;
  }

  if (!selected) {
    if (options.frontbuffer_index) {
      std::cerr << "frontbuffer snapshot index " << *options.frontbuffer_index
                << " was not found\n";
    } else {
      std::cerr << "no PM4 swap frontbuffer payloads are present\n";
    }
    return false;
  }

  const PM4SwapRecord &swap = selected->swap;
  const uint64_t required_bytes =
      static_cast<uint64_t>(swap.width) * swap.height * 4u;
  std::size_t nonzero_count = 0;
  std::size_t rgb_nonzero_pixels = 0;
  std::size_t alpha_nonzero_pixels = 0;
  std::optional<std::size_t> first_nonzero_offset;
  for (std::size_t i = 0; i < swap.frontbuffer_payload_bytes.size(); ++i) {
    if (swap.frontbuffer_payload_bytes[i] == 0) {
      continue;
    }
    if (!first_nonzero_offset) {
      first_nonzero_offset = i;
    }
    ++nonzero_count;
  }
  for (std::size_t i = 0; i + 3 < swap.frontbuffer_payload_bytes.size();
       i += 4) {
    if ((swap.frontbuffer_payload_bytes[i] |
         swap.frontbuffer_payload_bytes[i + 1] |
         swap.frontbuffer_payload_bytes[i + 2]) != 0) {
      ++rgb_nonzero_pixels;
    }
    if (swap.frontbuffer_payload_bytes[i + 3] != 0) {
      ++alpha_nonzero_pixels;
    }
  }

  const std::filesystem::path output =
      options.frontbuffer_output_path.empty()
          ? capture.path.parent_path() /
                ("frontbuffer-preview-" + std::to_string(selected_index) +
                 ".bmp")
          : options.frontbuffer_output_path;
  if (!output.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    if (ec) {
      std::cerr << "could not create frontbuffer output directory: "
                << ec.message() << "\n";
      return false;
    }
  }

  std::vector<uint8_t> output_rgba;
  std::string decode_reason;
  std::string decode_mode = "fetch0_tiled_rgba8";
  uint32_t output_width = swap.width;
  uint32_t output_height = swap.height;
  if (DecodeFrontbufferFetchRgba8(swap, output_rgba, decode_reason)) {
    output_width = swap.frontbuffer_fetch.width;
    output_height = swap.frontbuffer_fetch.height;
  } else {
    decode_mode = "raw_linear_rgba8";
    output_rgba = swap.frontbuffer_payload_bytes;
  }

  std::string error;
  if (!WriteRawLinearRgbaBmpPreview(output, output_rgba, output_width,
                                    output_height, error)) {
    std::cerr << "frontbuffer BMP preview failed: " << error << "\n";
    return false;
  }

  std::cout << "Frontbuffer dump snapshot=" << selected_index
            << " seq=" << selected->seq
            << " frontbuffer=" << FormatHex32(swap.frontbuffer_ptr)
            << " size=" << swap.width << "x" << swap.height
            << " payload=" << swap.frontbuffer_payload_bytes.size() << "/"
            << swap.frontbuffer_payload_requested_byte_count
            << (swap.frontbuffer_payload_loaded_from_resource ? " sidecar" : "")
            << (swap.frontbuffer_payload_truncated ? " truncated" : "")
            << " nonzero_bytes=" << nonzero_count
            << " rgb_nonzero_pixels=" << rgb_nonzero_pixels
            << " alpha_nonzero_pixels=" << alpha_nonzero_pixels;
  if (first_nonzero_offset) {
    std::cout << " first_nonzero_offset=" << *first_nonzero_offset;
  } else {
    std::cout << " all_zero";
  }
  if (required_bytes != swap.frontbuffer_payload_bytes.size()) {
    std::cout << " expected_linear_bytes=" << required_bytes;
  }
  std::cout << " decode_mode=" << decode_mode;
  if (!decode_reason.empty()) {
    std::cout << " decode_fallback_reason=\"" << decode_reason << "\"";
  }
  std::cout << "\n";
  if (swap.frontbuffer_fetch_valid) {
    const TextureFetchRecord &fetch = swap.frontbuffer_fetch;
    std::cout << "Frontbuffer fetch0: base="
              << FormatHex32(fetch.base_address_bytes)
              << " mip=" << FormatHex32(fetch.mip_address_bytes)
              << " format=" << fetch.format
              << " endian=" << fetch.endian
              << " tiled=" << (fetch.tiled ? "yes" : "no")
              << " pitch=" << fetch.pitch
              << " size=" << fetch.width << "x" << fetch.height
              << " depth_or_stack=" << fetch.depth_or_stack
              << " dimension=" << fetch.dimension
              << " swizzle=" << FormatHex32(fetch.swizzle)
              << " clamp=" << fetch.clamp_x << "/" << fetch.clamp_y << "/"
              << fetch.clamp_z << "\n";
  } else {
    std::cout << "Frontbuffer fetch0: missing\n";
  }
  std::size_t output_rgb_nonzero_pixels = 0;
  std::size_t output_alpha_nonzero_pixels = 0;
  for (std::size_t i = 0; i + 3 < output_rgba.size(); i += 4) {
    if ((output_rgba[i] | output_rgba[i + 1] | output_rgba[i + 2]) != 0) {
      ++output_rgb_nonzero_pixels;
    }
    if (output_rgba[i + 3] != 0) {
      ++output_alpha_nonzero_pixels;
    }
  }
  std::cout << "Frontbuffer decoded pixels: rgb_nonzero="
            << output_rgb_nonzero_pixels
            << " alpha_nonzero=" << output_alpha_nonzero_pixels << "\n";
  std::cout << "Frontbuffer BMP preview: "
            << std::filesystem::absolute(output).string() << "\n";
  if (decode_mode == "raw_linear_rgba8") {
    std::cout << "Frontbuffer note: using raw linear fallback; correct "
                 "swap-texture decode needs supported fetch0 metadata.\n";
  }
  return true;
}

bool DumpRenderTargetPreview(const ReplayCapture &capture,
                             const ReplayCliOptions &options) {
  if (!options.draw_index) {
    std::cerr << "--dump-render-target requires --draw <index>\n";
    return false;
  }
  if (*options.draw_index >= capture.draws.size()) {
    std::cerr << "draw " << *options.draw_index
              << " does not exist; capture has " << capture.draws.size()
              << " draws\n";
    return false;
  }

  const ReplayDrawState &draw_state = capture.draws[*options.draw_index];
  const RenderStateRecord &render_state = draw_state.draw.render_state;
  if (!render_state.present) {
    std::cerr << "draw " << *options.draw_index
              << " has no captured render state\n";
    return false;
  }

  const std::size_t target_index = options.render_target_index.value_or(0);
  if (target_index >= render_state.color_target_payloads.size()) {
    std::cerr << "draw " << *options.draw_index << " has "
              << render_state.color_target_payloads.size()
              << " captured color target slots; requested " << target_index
              << "\n";
    return false;
  }

  const RenderTargetPayloadRecord &payload =
      render_state.color_target_payloads[target_index];
  if (payload.payload_missing || payload.payload_bytes.empty()) {
    std::cerr << "draw " << *options.draw_index << " color target "
              << target_index << " payload is missing\n";
    return false;
  }
  if (payload.payload_truncated) {
    std::cerr << "draw " << *options.draw_index << " color target "
              << target_index << " payload is truncated\n";
    return false;
  }

  const uint32_t width = render_state.surface_pitch;
  if (width == 0) {
    std::cerr << "draw " << *options.draw_index
              << " has zero surface_pitch; cannot infer target width\n";
    return false;
  }
  const uint64_t required_bytes =
      payload.payload_requested_byte_count != 0
          ? payload.payload_requested_byte_count
          : static_cast<uint64_t>(payload.payload_bytes.size());
  const uint64_t row_bytes = static_cast<uint64_t>(width) * 4u;
  if (row_bytes == 0 || required_bytes < row_bytes ||
      required_bytes % row_bytes != 0) {
    std::cerr << "draw " << *options.draw_index
              << " color target payload size cannot be mapped as linear "
                 "RGBA8: bytes="
              << required_bytes << " width=" << width << "\n";
    return false;
  }
  const uint32_t height = static_cast<uint32_t>(required_bytes / row_bytes);

  const std::filesystem::path output =
      options.render_target_output_path.empty()
          ? capture.path.parent_path() /
                ("draw-" + std::to_string(*options.draw_index) +
                 "-rt" + std::to_string(target_index) + ".bmp")
          : options.render_target_output_path;
  if (!output.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    if (ec) {
      std::cerr << "could not create render-target output directory: "
                << ec.message() << "\n";
      return false;
    }
  }

  std::size_t nonzero_bytes = 0;
  std::size_t rgb_nonzero_pixels = 0;
  std::size_t alpha_nonzero_pixels = 0;
  for (std::size_t i = 0; i < payload.payload_bytes.size(); ++i) {
    if (payload.payload_bytes[i] != 0) {
      ++nonzero_bytes;
    }
  }
  for (std::size_t i = 0; i + 3 < payload.payload_bytes.size(); i += 4) {
    if ((payload.payload_bytes[i] | payload.payload_bytes[i + 1] |
         payload.payload_bytes[i + 2]) != 0) {
      ++rgb_nonzero_pixels;
    }
    if (payload.payload_bytes[i + 3] != 0) {
      ++alpha_nonzero_pixels;
    }
  }

  std::string error;
  if (!WriteRawLinearRgbaBmpPreview(output, payload.payload_bytes, width,
                                    height, error)) {
    std::cerr << "render-target BMP preview failed: " << error << "\n";
    return false;
  }

  std::cout << "Render target dump draw=" << *options.draw_index
            << " target=" << target_index
            << " base=" << payload.base
            << " size=" << width << "x" << height
            << " payload=" << payload.payload_bytes.size() << "/"
            << payload.payload_requested_byte_count
            << (payload.payload_loaded_from_resource ? " sidecar" : "")
            << " offset=" << payload.payload_offset_bytes
            << " nonzero_bytes=" << nonzero_bytes
            << " rgb_nonzero_pixels=" << rgb_nonzero_pixels
            << " alpha_nonzero_pixels=" << alpha_nonzero_pixels
            << " decode_mode=raw_linear_rgba8\n";
  std::cout << "Render target BMP preview: "
            << std::filesystem::absolute(output).string() << "\n";
  return true;
}

void PrintShaderRecordProbes(const ReplayCapture &capture,
                             std::size_t max_count) {
  std::cout << "Shader record probes:\n";
  std::size_t printed = 0;
  uint64_t primary_snapshots = 0;
  uint64_t secondary_snapshots = 0;
  uint64_t tertiary_snapshots = 0;
  uint64_t quaternary_snapshots = 0;
  uint64_t heap_candidate_snapshots = 0;
  for (const CaptureEvent &event : capture.events) {
    if (event.type != CaptureEventType::ShaderRecordProbe) {
      continue;
    }
    const ShaderRecordProbeRecord &probe = event.shader_probe;
    if (!probe.primary_missing && !probe.primary_dwords.empty()) {
      ++primary_snapshots;
    }
    if (!probe.secondary_missing && !probe.secondary_dwords.empty()) {
      ++secondary_snapshots;
    }
    if (!probe.tertiary_missing && !probe.tertiary_dwords.empty()) {
      ++tertiary_snapshots;
    }
    if (!probe.quaternary_missing && !probe.quaternary_dwords.empty()) {
      ++quaternary_snapshots;
    }
    if (!probe.heap_candidate_missing &&
        !probe.heap_candidate_dwords.empty()) {
      ++heap_candidate_snapshots;
    }
    if (printed >= max_count) {
      continue;
    }
    ++printed;
    std::cout << "  probe[" << printed - 1 << "] seq=" << event.seq
              << " event=" << probe.event << " " << probe.function
              << "(" << FormatHex32(probe.function_address) << ")"
              << " lr=" << FormatHex64(probe.link_register) << "\n";
    std::cout << "    regs r3=" << FormatHex32(probe.r3)
              << " r4=" << FormatHex32(probe.r4)
              << " r5=" << FormatHex32(probe.r5)
              << " r6=" << FormatHex32(probe.r6)
              << " r7=" << FormatHex32(probe.r7)
              << " r8=" << FormatHex32(probe.r8)
              << " r9=" << FormatHex32(probe.r9)
              << " r10=" << FormatHex32(probe.r10)
              << " r30=" << FormatHex32(probe.r30) << "\n";
    std::cout << "    write " << FormatHex32(probe.write_begin) << " -> "
              << FormatHex32(probe.write_end) << " limit "
              << FormatHex32(probe.write_limit_begin) << " -> "
              << FormatHex32(probe.write_limit_end)
              << " ret=" << FormatHex32(probe.return_value) << "\n";
    auto print_dwords = [](const char *label, uint32_t address,
                           uint32_t count, bool missing, bool truncated,
                           const std::vector<uint32_t> &dwords) {
      std::cout << "    " << label << "=" << FormatHex32(address)
                << " dwords=" << count
                << " missing=" << (missing ? "yes" : "no")
                << " truncated=" << (truncated ? "yes" : "no");
      if (!dwords.empty()) {
        std::cout << " first=";
        const std::size_t preview = std::min<std::size_t>(dwords.size(), 8);
        for (std::size_t i = 0; i < preview; ++i) {
          if (i) {
            std::cout << ",";
          }
          std::cout << FormatHex32(dwords[i]);
        }
        if (dwords.size() > preview) {
          std::cout << ",...";
        }
      }
      std::cout << "\n";
      const std::vector<std::string> ascii_runs =
          ExtractAsciiRunsFromDwords(dwords);
      if (!ascii_runs.empty()) {
        std::cout << "      ascii=\"";
        for (std::size_t i = 0; i < ascii_runs.size(); ++i) {
          if (i) {
            std::cout << "\" \"";
          }
          std::cout << ascii_runs[i];
        }
        std::cout << "\"\n";
        for (const std::string &run : ascii_runs) {
          const std::size_t name_pos = run.find("pimp_shader_");
          if (name_pos != std::string::npos) {
            std::cout << "      shader_name=" << run.substr(name_pos) << "\n";
            break;
          }
        }
      }
    };
    print_dwords("primary", probe.primary_address,
                 probe.primary_dword_count, probe.primary_missing,
                 probe.primary_truncated, probe.primary_dwords);
    print_dwords("secondary", probe.secondary_address,
                 probe.secondary_dword_count, probe.secondary_missing,
                 probe.secondary_truncated, probe.secondary_dwords);
    print_dwords("tertiary", probe.tertiary_address,
                 probe.tertiary_dword_count, probe.tertiary_missing,
                 probe.tertiary_truncated, probe.tertiary_dwords);
    print_dwords("quaternary", probe.quaternary_address,
                 probe.quaternary_dword_count, probe.quaternary_missing,
                 probe.quaternary_truncated, probe.quaternary_dwords);
    print_dwords("heap_candidate", probe.heap_candidate_address,
                 probe.heap_candidate_dword_count,
                 probe.heap_candidate_missing,
                 probe.heap_candidate_truncated,
                 probe.heap_candidate_dwords);
  }
  const auto total_it =
      capture.summary.event_counts.find(CaptureEventType::ShaderRecordProbe);
  const uint64_t total =
      total_it == capture.summary.event_counts.end() ? 0 : total_it->second;
  std::cout << "  total=" << total
            << " primary_snapshots=" << primary_snapshots
            << " secondary_snapshots=" << secondary_snapshots
            << " tertiary_snapshots=" << tertiary_snapshots
            << " quaternary_snapshots=" << quaternary_snapshots
            << " heap_candidate_snapshots=" << heap_candidate_snapshots
            << "\n";
  if (printed == 0) {
    std::cout << "  no shader_record_probe events captured\n";
  }
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
    } else if (arg == "--dump-frontbuffer") {
      cli.dump_frontbuffer = true;
    } else if (arg == "--dump-texture") {
      cli.dump_texture = true;
    } else if (arg == "--dump-render-target") {
      cli.dump_render_target = true;
    } else if (arg == "--resource-summary") {
      cli.show_resource_summary = true;
    } else if (arg == "--shader-usage") {
      cli.show_shader_usage = true;
    } else if (arg == "--missing-shaders") {
      cli.show_missing_shaders = true;
    } else if (arg == "--real-backend-gaps") {
      cli.show_real_backend_gaps = true;
    } else if (arg == "--shader-record-probes") {
      cli.show_shader_record_probes = true;
    } else if (arg == "--validate") {
      cli.validate_only = true;
    } else if (arg == "--allow-diagnostic-shader") {
      cli.allow_diagnostic_shader = true;
    } else if (arg == "--strict") {
      cli.strict_frame_replay = true;
    } else if (arg == "--skip-unsupported") {
      cli.skip_unsupported = true;
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
    } else if (arg == "--frontbuffer-index") {
      const char *value = require_value("--frontbuffer-index");
      std::size_t parsed = 0;
      if (!value || !ParseSizeArgument(value, parsed)) {
        std::cerr << "--frontbuffer-index expects an integer\n";
        return 2;
      }
      cli.frontbuffer_index = parsed;
      cli.dump_frontbuffer = true;
    } else if (arg == "--texture-slot") {
      const char *value = require_value("--texture-slot");
      std::size_t parsed = 0;
      if (!value || !ParseSizeArgument(value, parsed)) {
        std::cerr << "--texture-slot expects an integer\n";
        return 2;
      }
      cli.texture_slot = parsed;
      cli.dump_texture = true;
    } else if (arg == "--render-target-index") {
      const char *value = require_value("--render-target-index");
      std::size_t parsed = 0;
      if (!value || !ParseSizeArgument(value, parsed)) {
        std::cerr << "--render-target-index expects an integer\n";
        return 2;
      }
      cli.render_target_index = parsed;
      cli.dump_render_target = true;
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
    } else if (arg == "--shader-pair") {
      const char *value = require_value("--shader-pair");
      uint64_t vs = 0;
      uint64_t ps = 0;
      if (!value || !ParseShaderPairArgument(value, vs, ps)) {
        std::cerr << "--shader-pair expects <vs_hash>:<ps_hash>\n";
        return 2;
      }
      cli.shader_pair_vertex_hash = vs;
      cli.shader_pair_pixel_hash = ps;
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
    } else if (arg == "--d3d12-depth-output") {
      const char *value = require_value("--d3d12-depth-output");
      if (!value) {
        return 2;
      }
      cli.d3d12_depth_output_path = value;
    } else if (arg == "--frontbuffer-output") {
      const char *value = require_value("--frontbuffer-output");
      if (!value) {
        return 2;
      }
      cli.frontbuffer_output_path = value;
      cli.dump_frontbuffer = true;
    } else if (arg == "--texture-output") {
      const char *value = require_value("--texture-output");
      if (!value) {
        return 2;
      }
      cli.texture_output_path = value;
      cli.dump_texture = true;
    } else if (arg == "--render-target-output") {
      const char *value = require_value("--render-target-output");
      if (!value) {
        return 2;
      }
      cli.render_target_output_path = value;
      cli.dump_render_target = true;
    } else if (arg == "--shader-override-root") {
      const char *value = require_value("--shader-override-root");
      if (!value) {
        return 2;
      }
      cli.shader_override_root = value;
    } else if (arg == "--shader-cache-root") {
      const char *value = require_value("--shader-cache-root");
      if (!value) {
        return 2;
      }
      cli.shader_cache_root = value;
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
    if (!clean || !capture.errors.empty()) {
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
    if (cli.allow_diagnostic_shader) {
      std::cout << "D3D12 real replay note: using explicit diagnostic shader "
                   "fallback; output is resource-backed geometry, not "
                   "shader-correct BO2 rendering.\n";
    }
    std::string backend_error;
    if (!RunD3D12RealReplayBackend(capture, cli, backend_error)) {
      std::cerr << "D3D12 real replay unavailable: " << backend_error << "\n";
      return 1;
    }
    if (!cli.allow_diagnostic_shader) {
      std::cout << "D3D12 real replay note: used native shader cache root "
                << std::filesystem::absolute(cli.shader_cache_root).string()
                << " and override root "
                << std::filesystem::absolute(cli.shader_override_root).string()
                << "\n";
    }
    const std::filesystem::path output =
        cli.d3d12_output_path.empty()
            ? capture.path.parent_path() / "native-renderer-d3d12-replay.bmp"
            : cli.d3d12_output_path;
    std::cout << "D3D12 real replay output: "
              << std::filesystem::absolute(output).string() << "\n";
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
  if (cli.dump_frontbuffer) {
    std::cout << "\n";
    if (!DumpFrontbufferPreview(capture, cli)) {
      return 1;
    }
  }
  if (cli.dump_texture) {
    if (!cli.draw_index) {
      std::cerr << "--dump-texture requires --draw <index>\n";
      return 2;
    }
    std::cout << "\n";
    std::string texture_error;
    if (!DumpD3D12DecodedTexturePreview(capture, cli, texture_error)) {
      std::cerr << "texture preview failed: " << texture_error << "\n";
      return 1;
    }
  }
  if (cli.dump_render_target) {
    std::cout << "\n";
    if (!DumpRenderTargetPreview(capture, cli)) {
      return 1;
    }
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
  if (cli.show_real_backend_gaps) {
    std::cout << "\n";
    PrintD3D12RealBackendGaps(capture, cli);
  }
  if (cli.show_shader_record_probes) {
    std::cout << "\n";
    PrintShaderRecordProbes(capture, cli.max_draws);
  }

  return clean ? 0 : 1;
}

} // namespace bo2::native::replay
