#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {

struct CliOptions {
  std::filesystem::path index_path = "shader_work/shaders/index.json";
  std::filesystem::path capture_path;
  bool show_summary = false;
  bool find_hash = false;
  bool list_runtime_shaders = false;
  bool match_runtime_shaders = false;
  std::string hash;
  std::size_t limit = 8;
  std::size_t top_shaders = 20;
};

struct RuntimeShaderUsage {
  uint32_t stage = 0;
  uint64_t hash = 0;
  uint64_t draw_count = 0;
  uint64_t load_count = 0;
  uint32_t max_dwords = 0;
  uint64_t payload_load_count = 0;
  uint64_t payload_missing_count = 0;
  uint64_t payload_dwords = 0;
  uint64_t payload_truncated_count = 0;
  uint32_t max_payload_dwords = 0;
  std::string payload_sha256_le;
  std::string payload_sha256_be;
  std::string payload_trimmed_sha256_le;
  std::string payload_trimmed_sha256_be;
  uint64_t payload_hash_mismatch_count = 0;
};

struct RuntimeShaderPairUsage {
  uint64_t vertex_hash = 0;
  uint64_t pixel_hash = 0;
  uint64_t draw_count = 0;
};

struct ShaderRecordProbeUsage {
  uint64_t seq = 0;
  uint64_t event = 0;
  std::string function;
  uint32_t function_address = 0;
  uint64_t link_register = 0;
  uint32_t primary_address = 0;
  uint32_t secondary_address = 0;
  std::string shader_name;
  std::string shader_name_suffix;
  std::string shader_name_family;
  std::string shader_name_short_hash;
  std::string shader_name_entry;
  std::string shader_name_profile;
  std::string stage_guess;
  uint32_t primary_dword_count = 0;
  uint32_t secondary_dword_count = 0;
  std::string secondary_sha256_le;
  std::string secondary_sha256_be;
  std::string secondary_trimmed_sha256_le;
  std::string secondary_trimmed_sha256_be;
};

struct RuntimeShaderCapture {
  std::filesystem::path path;
  uint64_t lines = 0;
  uint64_t shader_events = 0;
  uint64_t draw_events = 0;
  uint64_t shader_record_probe_events = 0;
  uint64_t shader_payload_loads = 0;
  uint64_t shader_payload_missing = 0;
  uint64_t shader_payload_dwords = 0;
  uint64_t shader_payload_truncated = 0;
  std::vector<RuntimeShaderUsage> shaders;
  std::vector<RuntimeShaderPairUsage> pairs;
  std::vector<ShaderRecordProbeUsage> probes;
};

std::string ToLower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool ParseSize(std::string_view text, std::size_t &out) {
  std::size_t value = 0;
  for (char ch : text) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    value = value * 10 + static_cast<std::size_t>(ch - '0');
  }
  out = value;
  return true;
}

void PrintHelp() {
  std::cout << "native_shader_inspect --index <shader_work/shaders/index.json> "
               "[options]\n\n"
            << "Options:\n"
            << "  --summary          Print extracted shader index summary\n"
            << "  --capture <path>   JSONL native renderer capture to inspect\n"
            << "  --list-runtime-shaders\n"
            << "                     Rank runtime shader hashes from --capture\n"
            << "  --match-runtime-shaders\n"
            << "                     Search the static index for runtime hashes\n"
            << "  --top-shaders <n>  Runtime shader/pair print limit (default 20)\n"
            << "  --hash <value>     Hash or substring to find\n"
            << "  --find             Search the index for --hash\n"
            << "  --limit <count>    Limit matching records (default 8)\n"
            << "  --help             Show this help\n";
}

std::optional<std::filesystem::path>
ResolveIndexPath(std::filesystem::path path) {
  if (std::filesystem::exists(path)) {
    return std::filesystem::absolute(path);
  }
  if (path.filename() == "index.json") {
    std::filesystem::path fallback =
        path.parent_path() / "shaders" / "index.json";
    if (std::filesystem::exists(fallback)) {
      return std::filesystem::absolute(fallback);
    }
  }
  return std::nullopt;
}

bool LoadText(const std::filesystem::path &path, std::string &text) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  text = ss.str();
  return true;
}

std::optional<uint64_t> ParseHexU64(std::string text) {
  if (text.empty()) {
    return std::nullopt;
  }
  if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) {
    text.erase(0, 2);
  }
  try {
    return std::stoull(text, nullptr, 16);
  } catch (...) {
    return std::nullopt;
  }
}

uint32_t ParseU32HexOrZero(const std::string &text) {
  if (auto value = ParseHexU64(text)) {
    return static_cast<uint32_t>(*value);
  }
  return 0;
}

uint32_t ParseU32OrZero(std::string text) {
  if (text.empty()) {
    return 0;
  }
  try {
    return static_cast<uint32_t>(std::stoul(text));
  } catch (...) {
    return 0;
  }
}

std::vector<std::string> ExtractJsonStringArray(std::string_view block,
                                                std::string_view key) {
  std::vector<std::string> values;
  const std::string needle = "\"" + std::string(key) + "\"";
  std::size_t pos = block.find(needle);
  if (pos == std::string_view::npos) {
    return values;
  }
  pos = block.find(':', pos + needle.size());
  if (pos == std::string_view::npos) {
    return values;
  }
  pos = block.find('[', pos + 1);
  if (pos == std::string_view::npos) {
    return values;
  }
  const std::size_t end = block.find(']', pos + 1);
  if (end == std::string_view::npos) {
    return values;
  }

  while (pos < end) {
    pos = block.find('"', pos + 1);
    if (pos == std::string_view::npos || pos >= end) {
      break;
    }
    const std::size_t begin = pos + 1;
    pos = block.find('"', begin);
    if (pos == std::string_view::npos || pos > end) {
      break;
    }
    values.emplace_back(block.substr(begin, pos - begin));
  }
  return values;
}

std::string ExtractJsonString(std::string_view block, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  std::size_t pos = block.find(needle);
  if (pos == std::string_view::npos) {
    return {};
  }
  pos = block.find(':', pos + needle.size());
  if (pos == std::string_view::npos) {
    return {};
  }
  pos = block.find('"', pos + 1);
  if (pos == std::string_view::npos) {
    return {};
  }
  const std::size_t begin = pos + 1;
  pos = block.find('"', begin);
  if (pos == std::string_view::npos) {
    return {};
  }
  return std::string(block.substr(begin, pos - begin));
}

std::string ExtractJsonNumberText(std::string_view block,
                                  std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  std::size_t pos = block.find(needle);
  if (pos == std::string_view::npos) {
    return {};
  }
  pos = block.find(':', pos + needle.size());
  if (pos == std::string_view::npos) {
    return {};
  }
  ++pos;
  while (pos < block.size() &&
         std::isspace(static_cast<unsigned char>(block[pos]))) {
    ++pos;
  }
  const std::size_t begin = pos;
  while (pos < block.size() &&
         (std::isdigit(static_cast<unsigned char>(block[pos])) ||
          block[pos] == '-')) {
    ++pos;
  }
  return std::string(block.substr(begin, pos - begin));
}

std::optional<bool> ExtractJsonBool(std::string_view block,
                                    std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  std::size_t pos = block.find(needle);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  pos = block.find(':', pos + needle.size());
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  ++pos;
  while (pos < block.size() &&
         std::isspace(static_cast<unsigned char>(block[pos]))) {
    ++pos;
  }
  if (block.substr(pos, 4) == "true") {
    return true;
  }
  if (block.substr(pos, 5) == "false") {
    return false;
  }
  return std::nullopt;
}

uint64_t ExtractJsonHexU64(std::string_view block, std::string_view key) {
  if (auto value = ParseHexU64(ExtractJsonString(block, key))) {
    return *value;
  }
  return 0;
}

std::string HexBytes(const uint8_t *bytes, std::size_t count) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::nouppercase;
  for (std::size_t i = 0; i < count; ++i) {
    out << std::setw(2) << static_cast<uint32_t>(bytes[i]);
  }
  return out.str();
}

std::string Sha256Hex(const std::vector<uint8_t> &bytes) {
#if defined(_WIN32)
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::array<uint8_t, 32> digest{};
  DWORD hash_length = 0;
  DWORD result_size = 0;

  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  0) < 0) {
    return {};
  }
  auto close_algorithm = [&]() {
    if (algorithm) {
      BCryptCloseAlgorithmProvider(algorithm, 0);
    }
  };

  if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                        reinterpret_cast<PUCHAR>(&hash_length),
                        sizeof(hash_length), &result_size, 0) < 0 ||
      hash_length != digest.size()) {
    close_algorithm();
    return {};
  }
  if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
    close_algorithm();
    return {};
  }
  const auto destroy_hash = [&]() {
    if (hash) {
      BCryptDestroyHash(hash);
    }
  };
  if (!bytes.empty() &&
      BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
                     static_cast<ULONG>(bytes.size()), 0) < 0) {
    destroy_hash();
    close_algorithm();
    return {};
  }
  if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()),
                       0) < 0) {
    destroy_hash();
    close_algorithm();
    return {};
  }
  destroy_hash();
  close_algorithm();
  return HexBytes(digest.data(), digest.size());
#else
  (void)bytes;
  return {};
#endif
}

std::vector<uint8_t> DwordsToBytes(const std::vector<uint32_t> &dwords,
                                   bool little_endian) {
  std::vector<uint8_t> bytes;
  bytes.reserve(dwords.size() * 4);
  for (uint32_t dword : dwords) {
    if (little_endian) {
      bytes.push_back(static_cast<uint8_t>(dword & 0xFFu));
      bytes.push_back(static_cast<uint8_t>((dword >> 8) & 0xFFu));
      bytes.push_back(static_cast<uint8_t>((dword >> 16) & 0xFFu));
      bytes.push_back(static_cast<uint8_t>((dword >> 24) & 0xFFu));
    } else {
      bytes.push_back(static_cast<uint8_t>((dword >> 24) & 0xFFu));
      bytes.push_back(static_cast<uint8_t>((dword >> 16) & 0xFFu));
      bytes.push_back(static_cast<uint8_t>((dword >> 8) & 0xFFu));
      bytes.push_back(static_cast<uint8_t>(dword & 0xFFu));
    }
  }
  return bytes;
}

std::vector<uint32_t> ParseDwordArray(std::string_view line,
                                      std::string_view key) {
  std::vector<uint32_t> dwords;
  for (const std::string &text : ExtractJsonStringArray(line, key)) {
    dwords.push_back(ParseU32HexOrZero(text));
  }
  return dwords;
}

std::vector<uint32_t> ParsePayloadDwords(std::string_view line) {
  return ParseDwordArray(line, "dwords");
}

std::vector<uint32_t> TrimTrailingZeroDwords(std::vector<uint32_t> dwords) {
  while (!dwords.empty() && dwords.back() == 0) {
    dwords.pop_back();
  }
  return dwords;
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

std::string ExtractShaderNameFromDwords(const std::vector<uint32_t> &dwords) {
  for (const std::string &run : ExtractAsciiRunsFromDwords(dwords)) {
    const std::size_t pos = run.find("pimp_shader_");
    if (pos != std::string::npos) {
      return run.substr(pos);
    }
  }
  return {};
}

bool IsHexString(std::string_view text) {
  return std::all_of(text.begin(), text.end(), [](unsigned char ch) {
    return std::isxdigit(ch) != 0;
  });
}

std::vector<std::string> SplitString(std::string_view text, char delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find(delimiter, start);
    if (end == std::string_view::npos) {
      parts.emplace_back(text.substr(start));
      break;
    }
    parts.emplace_back(text.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

std::string JoinTokens(const std::vector<std::string> &tokens,
                       std::size_t begin, std::size_t end) {
  std::string joined;
  for (std::size_t i = begin; i < end && i < tokens.size(); ++i) {
    if (!joined.empty()) {
      joined.push_back('_');
    }
    joined += tokens[i];
  }
  return joined;
}

std::string ExtractShaderNameSuffix(std::string_view name) {
  const std::size_t dot = name.rfind(".updb");
  const std::size_t end = dot == std::string_view::npos ? name.size() : dot;
  const std::size_t underscore = name.rfind('_', end);
  if (underscore == std::string_view::npos || underscore + 1 >= end) {
    return {};
  }
  const std::string suffix(name.substr(underscore + 1, end - underscore - 1));
  if (suffix.size() == 32 && IsHexString(suffix)) {
    return suffix;
  }
  return {};
}

void ExtractShaderNameMetadata(std::string_view name,
                               ShaderRecordProbeUsage &probe) {
  const std::size_t dot = name.rfind(".updb");
  const std::size_t end = dot == std::string_view::npos ? name.size() : dot;
  const std::vector<std::string> tokens = SplitString(name.substr(0, end), '_');
  if (tokens.size() < 8 || tokens[0] != "pimp" || tokens[1] != "shader") {
    return;
  }

  for (std::size_t i = 2; i < tokens.size(); ++i) {
    const bool likely_short_hash =
        tokens[i].size() >= 6 && tokens[i].size() <= 8 &&
        IsHexString(tokens[i]);
    if (!likely_short_hash || i + 5 >= tokens.size()) {
      continue;
    }

    probe.shader_name_family = JoinTokens(tokens, 2, i);
    probe.shader_name_short_hash = tokens[i];
    probe.shader_name_entry = tokens[i + 2];
    probe.shader_name_profile = JoinTokens(tokens, i + 3, i + 6);
    return;
  }
}

std::string GuessStageFromShaderName(std::string_view name) {
  if (name.find("_ps_main_") != std::string_view::npos ||
      name.find("_ps_3_0_") != std::string_view::npos) {
    return "PS";
  }
  if (name.find("_vs_main_") != std::string_view::npos ||
      name.find("_vs_3_0_") != std::string_view::npos) {
    return "VS";
  }
  return "??";
}

void RecordPayloadHashes(RuntimeShaderUsage &usage,
                         const std::vector<uint32_t> &dwords) {
  if (dwords.empty()) {
    return;
  }

  const std::string raw_le = Sha256Hex(DwordsToBytes(dwords, true));
  const std::string raw_be = Sha256Hex(DwordsToBytes(dwords, false));
  const std::vector<uint32_t> trimmed = TrimTrailingZeroDwords(dwords);
  const std::string trimmed_le = Sha256Hex(DwordsToBytes(trimmed, true));
  const std::string trimmed_be = Sha256Hex(DwordsToBytes(trimmed, false));

  if (usage.payload_sha256_le.empty()) {
    usage.payload_sha256_le = raw_le;
    usage.payload_sha256_be = raw_be;
    usage.payload_trimmed_sha256_le = trimmed_le;
    usage.payload_trimmed_sha256_be = trimmed_be;
    return;
  }

  if (usage.payload_sha256_le != raw_le || usage.payload_sha256_be != raw_be ||
      usage.payload_trimmed_sha256_le != trimmed_le ||
      usage.payload_trimmed_sha256_be != trimmed_be) {
    ++usage.payload_hash_mismatch_count;
  }
}

void RecordProbeSecondaryHashes(ShaderRecordProbeUsage &usage,
                                const std::vector<uint32_t> &dwords) {
  if (dwords.empty()) {
    return;
  }
  usage.secondary_sha256_le = Sha256Hex(DwordsToBytes(dwords, true));
  usage.secondary_sha256_be = Sha256Hex(DwordsToBytes(dwords, false));
  const std::vector<uint32_t> trimmed = TrimTrailingZeroDwords(dwords);
  usage.secondary_trimmed_sha256_le =
      Sha256Hex(DwordsToBytes(trimmed, true));
  usage.secondary_trimmed_sha256_be =
      Sha256Hex(DwordsToBytes(trimmed, false));
}

std::optional<std::string_view> RecordAround(std::string_view text,
                                             std::size_t pos) {
  std::size_t begin = text.rfind("\n    {", pos);
  if (begin == std::string_view::npos) {
    begin = text.rfind('{', pos);
  }
  if (begin == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t end = text.find("\n    }", pos);
  if (end == std::string_view::npos) {
    end = text.find('}', pos);
  }
  if (end == std::string_view::npos || end <= begin) {
    return std::nullopt;
  }
  return text.substr(begin, end - begin + 6);
}

std::optional<std::string_view> FindRecordByStage(std::string_view text,
                                                  std::string_view stage) {
  const std::string needle = "\"stage\": \"" + std::string(stage) + "\"";
  const std::size_t pos = text.find(needle);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  return RecordAround(text, pos);
}

void PrintRecord(std::string_view block, std::string_view prefix) {
  const std::string hash = ExtractJsonString(block, "hash");
  const std::string stage = ExtractJsonString(block, "stage");
  const std::string flags = ExtractJsonString(block, "flags");
  const std::string size = ExtractJsonNumberText(block, "size");
  const std::string file = ExtractJsonString(block, "file");
  const std::string micro_hash = ExtractJsonString(block, "microcode_hash");
  const std::string micro_size = ExtractJsonNumberText(block, "microcode_size");
  const std::string zone = ExtractJsonString(block, "zone");
  const std::string offset = ExtractJsonNumberText(block, "offset");

  std::cout << prefix << stage << " hash=" << hash << " flags=" << flags
            << " container_bytes=" << size << "\n";
  std::cout << prefix << "  file=" << file << "\n";
  std::cout << prefix << "  microcode_hash=" << micro_hash
            << " microcode_bytes=" << micro_size << "\n";
  if (!zone.empty() || !offset.empty()) {
    std::cout << prefix << "  first_source=" << zone << "@0x" << std::hex
              << std::uppercase;
    try {
      std::cout << std::stoull(offset);
    } catch (...) {
      std::cout << offset;
    }
    std::cout << std::dec << "\n";
  }
}

const char *StageName(uint32_t stage) {
  switch (stage) {
  case 0:
    return "VS";
  case 1:
    return "PS";
  default:
    return "??";
  }
}

bool LoadRuntimeShaderCapture(const std::filesystem::path &path,
                              RuntimeShaderCapture &capture,
                              std::string &error) {
  std::ifstream file(path);
  if (!file) {
    error = "could not read capture: " + path.string();
    return false;
  }

  capture = {};
  capture.path = std::filesystem::absolute(path);
  std::map<std::pair<uint32_t, uint64_t>, RuntimeShaderUsage> shaders;
  std::map<std::pair<uint64_t, uint64_t>, RuntimeShaderPairUsage> pairs;

  std::string line;
  while (std::getline(file, line)) {
    ++capture.lines;
    if (line.find("\"type\":\"pm4_shader\"") != std::string::npos) {
      ++capture.shader_events;
      const uint32_t stage =
          ParseU32OrZero(ExtractJsonNumberText(line, "shader_type"));
      const uint64_t hash = ExtractJsonHexU64(line, "shader_hash");
      if (hash == 0) {
        continue;
      }
      const uint32_t dwords =
          ParseU32OrZero(ExtractJsonNumberText(line, "dword_count"));
      const uint32_t payload_dwords =
          ParseU32OrZero(ExtractJsonNumberText(line, "payload_dword_count"));
      const bool payload_missing =
          ExtractJsonBool(line, "payload_missing").value_or(true);
      const bool payload_truncated =
          ExtractJsonBool(line, "payload_truncated").value_or(false);
      auto &usage = shaders[{stage, hash}];
      usage.stage = stage;
      usage.hash = hash;
      ++usage.load_count;
      usage.max_dwords = std::max(usage.max_dwords, dwords);
      if (payload_missing) {
        ++usage.payload_missing_count;
        ++capture.shader_payload_missing;
      } else {
        ++usage.payload_load_count;
        usage.payload_dwords += payload_dwords;
        usage.max_payload_dwords =
            std::max(usage.max_payload_dwords, payload_dwords);
        ++capture.shader_payload_loads;
        capture.shader_payload_dwords += payload_dwords;
        RecordPayloadHashes(usage, ParsePayloadDwords(line));
      }
      if (payload_truncated) {
        ++usage.payload_truncated_count;
        ++capture.shader_payload_truncated;
      }
    } else if (line.find("\"type\":\"shader_record_probe\"") !=
               std::string::npos) {
      ++capture.shader_record_probe_events;
      ShaderRecordProbeUsage probe{};
      probe.seq = ParseU32OrZero(ExtractJsonNumberText(line, "seq"));
      probe.event = ParseU32OrZero(ExtractJsonNumberText(line, "event"));
      probe.function = ExtractJsonString(line, "function");
      probe.function_address = static_cast<uint32_t>(
          ExtractJsonHexU64(line, "function_address"));
      probe.link_register = ExtractJsonHexU64(line, "link_register");
      probe.primary_address =
          static_cast<uint32_t>(ExtractJsonHexU64(line, "primary_address"));
      probe.secondary_address =
          static_cast<uint32_t>(ExtractJsonHexU64(line, "secondary_address"));
      probe.primary_dword_count =
          ParseU32OrZero(ExtractJsonNumberText(line, "primary_dword_count"));
      probe.secondary_dword_count =
          ParseU32OrZero(ExtractJsonNumberText(line, "secondary_dword_count"));
      const std::vector<uint32_t> primary_dwords =
          ParseDwordArray(line, "primary_dwords");
      const std::vector<uint32_t> secondary_dwords =
          ParseDwordArray(line, "secondary_dwords");
      probe.shader_name = ExtractShaderNameFromDwords(primary_dwords);
      probe.shader_name_suffix = ExtractShaderNameSuffix(probe.shader_name);
      probe.stage_guess = GuessStageFromShaderName(probe.shader_name);
      ExtractShaderNameMetadata(probe.shader_name, probe);
      RecordProbeSecondaryHashes(probe, secondary_dwords);
      capture.probes.push_back(std::move(probe));
    } else if (line.find("\"type\":\"pm4_draw\"") != std::string::npos) {
      ++capture.draw_events;
      const uint64_t vs = ExtractJsonHexU64(line, "vertex_shader_hash");
      const uint64_t ps = ExtractJsonHexU64(line, "pixel_shader_hash");
      if (vs != 0) {
        auto &usage = shaders[{0, vs}];
        usage.stage = 0;
        usage.hash = vs;
        ++usage.draw_count;
      }
      if (ps != 0) {
        auto &usage = shaders[{1, ps}];
        usage.stage = 1;
        usage.hash = ps;
        ++usage.draw_count;
      }
      if (vs != 0 || ps != 0) {
        auto &pair = pairs[{vs, ps}];
        pair.vertex_hash = vs;
        pair.pixel_hash = ps;
        ++pair.draw_count;
      }
    }
  }

  capture.shaders.reserve(shaders.size());
  for (const auto &entry : shaders) {
    capture.shaders.push_back(entry.second);
  }
  std::sort(capture.shaders.begin(), capture.shaders.end(),
            [](const RuntimeShaderUsage &lhs,
               const RuntimeShaderUsage &rhs) {
              if (lhs.draw_count != rhs.draw_count) {
                return lhs.draw_count > rhs.draw_count;
              }
              if (lhs.load_count != rhs.load_count) {
                return lhs.load_count > rhs.load_count;
              }
              if (lhs.stage != rhs.stage) {
                return lhs.stage < rhs.stage;
              }
              return lhs.hash < rhs.hash;
            });

  capture.pairs.reserve(pairs.size());
  for (const auto &entry : pairs) {
    capture.pairs.push_back(entry.second);
  }
  std::sort(capture.pairs.begin(), capture.pairs.end(),
            [](const RuntimeShaderPairUsage &lhs,
               const RuntimeShaderPairUsage &rhs) {
              if (lhs.draw_count != rhs.draw_count) {
                return lhs.draw_count > rhs.draw_count;
              }
              if (lhs.vertex_hash != rhs.vertex_hash) {
                return lhs.vertex_hash < rhs.vertex_hash;
              }
              return lhs.pixel_hash < rhs.pixel_hash;
            });

  return true;
}

void PrintRuntimeShaders(const RuntimeShaderCapture &capture,
                         std::size_t top_count) {
  std::cout << "Runtime shader capture: " << capture.path.string() << "\n";
  std::cout << "  lines=" << capture.lines
            << " shader_events=" << capture.shader_events
            << " draw_events=" << capture.draw_events
            << " unique_shaders=" << capture.shaders.size()
            << " shader_pairs=" << capture.pairs.size()
            << " shader_record_probes="
            << capture.shader_record_probe_events << "\n";
  std::cout << "  shader_payloads with_payload="
            << capture.shader_payload_loads
            << " missing_payload=" << capture.shader_payload_missing
            << " payload_dwords=" << capture.shader_payload_dwords
            << " truncated=" << capture.shader_payload_truncated << "\n";
  std::cout << "\nRuntime shaders:\n";
  const std::size_t shader_count =
      std::min<std::size_t>(top_count, capture.shaders.size());
  for (std::size_t i = 0; i < shader_count; ++i) {
    const RuntimeShaderUsage &usage = capture.shaders[i];
    std::cout << "  " << StageName(usage.stage) << " 0x" << std::hex
              << std::uppercase << usage.hash << std::dec
              << " draws=" << usage.draw_count
              << " loads=" << usage.load_count
              << " max_dwords=" << usage.max_dwords
              << " payload_loads=" << usage.payload_load_count
              << " missing_payload=" << usage.payload_missing_count
              << " payload_dwords=" << usage.payload_dwords
              << " max_payload=" << usage.max_payload_dwords
              << " payload_truncated=" << usage.payload_truncated_count
              << " payload_hash_mismatches="
              << usage.payload_hash_mismatch_count
              << "\n";
    if (!usage.payload_sha256_le.empty()) {
      std::cout << "    payload_sha256_le=" << usage.payload_sha256_le
                << "\n"
                << "    payload_sha256_be=" << usage.payload_sha256_be
                << "\n"
                << "    payload_trimmed_sha256_le="
                << usage.payload_trimmed_sha256_le << "\n"
                << "    payload_trimmed_sha256_be="
                << usage.payload_trimmed_sha256_be << "\n";
    }
  }

  std::cout << "\nRuntime shader pairs:\n";
  const std::size_t pair_count =
      std::min<std::size_t>(top_count, capture.pairs.size());
  for (std::size_t i = 0; i < pair_count; ++i) {
    const RuntimeShaderPairUsage &pair = capture.pairs[i];
    std::cout << "  VS=0x" << std::hex << std::uppercase << pair.vertex_hash
              << " PS=0x" << pair.pixel_hash << std::dec
              << " draws=" << pair.draw_count << "\n";
  }

  if (!capture.probes.empty()) {
    std::cout << "\nShader record probes:\n";
    const std::size_t probe_count =
        std::min<std::size_t>(top_count, capture.probes.size());
    for (std::size_t i = 0; i < probe_count; ++i) {
      const ShaderRecordProbeUsage &probe = capture.probes[i];
      std::cout << "  probe[" << i << "] seq=" << probe.seq
                << " event=" << probe.event << " " << probe.function
                << " stage_guess=" << probe.stage_guess
                << " primary=0x" << std::hex << std::uppercase
                << probe.primary_address << " secondary=0x"
                << probe.secondary_address << std::dec << "\n";
      if (!probe.shader_name.empty()) {
        std::cout << "    shader_name=" << probe.shader_name << "\n";
      }
      if (!probe.shader_name_suffix.empty()) {
        std::cout << "    shader_name_suffix=" << probe.shader_name_suffix
                  << "\n";
      }
      if (!probe.shader_name_short_hash.empty()) {
        std::cout << "    shader_name_family=" << probe.shader_name_family
                  << " short_hash=" << probe.shader_name_short_hash
                  << " entry=" << probe.shader_name_entry
                  << " profile=" << probe.shader_name_profile << "\n";
      }
      if (!probe.secondary_sha256_le.empty()) {
        std::cout << "    secondary_sha256_le="
                  << probe.secondary_sha256_le << "\n"
                  << "    secondary_sha256_be="
                  << probe.secondary_sha256_be << "\n"
                  << "    secondary_trimmed_sha256_le="
                  << probe.secondary_trimmed_sha256_le << "\n"
                  << "    secondary_trimmed_sha256_be="
                  << probe.secondary_trimmed_sha256_be << "\n";
      }
    }
  }
}

void MatchRuntimeShaders(std::string_view index_text,
                         const RuntimeShaderCapture &capture,
                         std::size_t top_count) {
  const std::string lower_index = ToLower(std::string(index_text));
  uint64_t matched = 0;
  std::cout << "Runtime shader index match:\n";
  const std::size_t count =
      std::min<std::size_t>(top_count, capture.shaders.size());
  for (std::size_t i = 0; i < count; ++i) {
    const RuntimeShaderUsage &usage = capture.shaders[i];
    std::ostringstream hash_stream;
    hash_stream << std::hex << std::nouppercase << usage.hash;
    std::string hash_text = hash_stream.str();
    while (hash_text.size() < 16) {
      hash_text.insert(hash_text.begin(), '0');
    }

    const std::size_t pos = lower_index.find(hash_text);
    const std::size_t raw_le_pos =
        usage.payload_sha256_le.empty()
            ? std::string::npos
            : lower_index.find(ToLower(usage.payload_sha256_le));
    const std::size_t raw_be_pos =
        usage.payload_sha256_be.empty()
            ? std::string::npos
            : lower_index.find(ToLower(usage.payload_sha256_be));
    const std::size_t trimmed_le_pos =
        usage.payload_trimmed_sha256_le.empty()
            ? std::string::npos
            : lower_index.find(ToLower(usage.payload_trimmed_sha256_le));
    const std::size_t trimmed_be_pos =
        usage.payload_trimmed_sha256_be.empty()
            ? std::string::npos
            : lower_index.find(ToLower(usage.payload_trimmed_sha256_be));
    std::cout << "  " << StageName(usage.stage) << " 0x" << std::hex
              << std::uppercase << usage.hash << std::dec
              << " draws=" << usage.draw_count
              << " loads=" << usage.load_count
              << " payload_loads=" << usage.payload_load_count
              << " missing_payload=" << usage.payload_missing_count
              << " exact_substring_match="
              << (pos == std::string::npos ? "no" : "yes")
              << " payload_raw_le_match="
              << (raw_le_pos == std::string::npos ? "no" : "yes")
              << " payload_raw_be_match="
              << (raw_be_pos == std::string::npos ? "no" : "yes")
              << " payload_trimmed_le_match="
              << (trimmed_le_pos == std::string::npos ? "no" : "yes")
              << " payload_trimmed_be_match="
              << (trimmed_be_pos == std::string::npos ? "no" : "yes")
              << " payload_hash_mismatches="
              << usage.payload_hash_mismatch_count << "\n";
    if (!usage.payload_sha256_le.empty()) {
      std::cout << "    payload_sha256_le=" << usage.payload_sha256_le
                << "\n"
                << "    payload_sha256_be=" << usage.payload_sha256_be
                << "\n"
                << "    payload_trimmed_sha256_le="
                << usage.payload_trimmed_sha256_le << "\n"
                << "    payload_trimmed_sha256_be="
                << usage.payload_trimmed_sha256_be << "\n";
    }
    if (pos != std::string::npos) {
      ++matched;
      if (auto record = RecordAround(index_text, pos)) {
        PrintRecord(*record, "    ");
      }
    }
  }
  std::cout << "Runtime shader direct matches: " << matched << "/" << count
            << "\n";
  if (matched != count) {
    std::cout << "Runtime 64-bit shader hashes are not proven to be static "
                 "container or microcode hashes. Next matching rules need the "
                 "captured PM4 shader payload bytes, byte-swapped payload "
                 "hashes, and shader record metadata.\n";
  }

  if (!capture.probes.empty()) {
    uint64_t probe_name_matches = 0;
    uint64_t probe_suffix_matches = 0;
    uint64_t probe_short_hash_matches = 0;
    uint64_t probe_secondary_matches = 0;
    std::cout << "\nShader record probe index match:\n";
    const std::size_t probe_count =
        std::min<std::size_t>(top_count, capture.probes.size());
    for (std::size_t i = 0; i < probe_count; ++i) {
      const ShaderRecordProbeUsage &probe = capture.probes[i];
      const std::string lower_name = ToLower(probe.shader_name);
      const std::string lower_suffix = ToLower(probe.shader_name_suffix);
      const std::string lower_short_hash =
          ToLower(probe.shader_name_short_hash);
      const bool name_match =
          !lower_name.empty() &&
          lower_index.find(lower_name) != std::string::npos;
      const bool suffix_match =
          !lower_suffix.empty() &&
          lower_index.find(lower_suffix) != std::string::npos;
      const bool short_hash_match =
          !lower_short_hash.empty() &&
          lower_index.find(lower_short_hash) != std::string::npos;
      const bool secondary_le_match =
          !probe.secondary_sha256_le.empty() &&
          lower_index.find(ToLower(probe.secondary_sha256_le)) !=
              std::string::npos;
      const bool secondary_be_match =
          !probe.secondary_sha256_be.empty() &&
          lower_index.find(ToLower(probe.secondary_sha256_be)) !=
              std::string::npos;
      const bool secondary_trimmed_le_match =
          !probe.secondary_trimmed_sha256_le.empty() &&
          lower_index.find(ToLower(probe.secondary_trimmed_sha256_le)) !=
              std::string::npos;
      const bool secondary_trimmed_be_match =
          !probe.secondary_trimmed_sha256_be.empty() &&
          lower_index.find(ToLower(probe.secondary_trimmed_sha256_be)) !=
              std::string::npos;
      if (name_match) {
        ++probe_name_matches;
      }
      if (suffix_match) {
        ++probe_suffix_matches;
      }
      if (short_hash_match) {
        ++probe_short_hash_matches;
      }
      if (secondary_le_match || secondary_be_match ||
          secondary_trimmed_le_match || secondary_trimmed_be_match) {
        ++probe_secondary_matches;
      }
      std::cout << "  probe[" << i << "] " << probe.stage_guess
                << " name=" << probe.shader_name
                << " name_match=" << (name_match ? "yes" : "no")
                << " suffix_match=" << (suffix_match ? "yes" : "no")
                << " short_hash_match="
                << (short_hash_match ? "yes" : "no")
                << " secondary_raw_le_match="
                << (secondary_le_match ? "yes" : "no")
                << " secondary_raw_be_match="
                << (secondary_be_match ? "yes" : "no")
                << " secondary_trimmed_le_match="
                << (secondary_trimmed_le_match ? "yes" : "no")
                << " secondary_trimmed_be_match="
                << (secondary_trimmed_be_match ? "yes" : "no") << "\n";
      if (!probe.shader_name_suffix.empty()) {
        std::cout << "    suffix=" << probe.shader_name_suffix << "\n";
      }
      if (!probe.shader_name_short_hash.empty()) {
        std::cout << "    family=" << probe.shader_name_family
                  << " short_hash=" << probe.shader_name_short_hash
                  << " entry=" << probe.shader_name_entry
                  << " profile=" << probe.shader_name_profile << "\n";
      }
      if (!probe.secondary_sha256_le.empty()) {
        std::cout << "    secondary_sha256_le="
                  << probe.secondary_sha256_le << "\n"
                  << "    secondary_sha256_be="
                  << probe.secondary_sha256_be << "\n"
                  << "    secondary_trimmed_sha256_le="
                  << probe.secondary_trimmed_sha256_le << "\n"
                  << "    secondary_trimmed_sha256_be="
                  << probe.secondary_trimmed_sha256_be << "\n";
      }
    }
    std::cout << "Shader record probe matches: names=" << probe_name_matches
              << "/" << probe_count << " suffixes=" << probe_suffix_matches
              << "/" << probe_count << " short_hashes="
              << probe_short_hash_matches << "/" << probe_count
              << " secondary_payloads=" << probe_secondary_matches << "/"
              << probe_count << "\n";
  }
}

void PrintSummary(std::string_view text, const std::filesystem::path &path) {
  std::cout << "Shader index: " << path.string() << "\n";
  const std::vector<std::string_view> keys = {
      "zones",
      "occurrences",
      "unique_containers",
      "pixel_containers",
      "vertex_containers",
      "unique_microcode",
      "pixel_microcode",
      "vertex_microcode",
      "container_bytes",
      "microcode_bytes",
      "invalid_bounds",
  };
  for (std::string_view key : keys) {
    const std::string value = ExtractJsonNumberText(text, key);
    if (!value.empty()) {
      std::cout << "  " << key << ": " << value << "\n";
    }
  }

  if (auto pixel = FindRecordByStage(text, "pixel")) {
    std::cout << "\nFirst pixel container:\n";
    PrintRecord(*pixel, "  ");
  }
  if (auto vertex = FindRecordByStage(text, "vertex")) {
    std::cout << "\nFirst vertex container:\n";
    PrintRecord(*vertex, "  ");
  }
}

void FindHash(std::string_view text, std::string needle, std::size_t limit) {
  needle = ToLower(std::move(needle));
  if (needle.rfind("0x", 0) == 0) {
    needle.erase(0, 2);
  }
  const std::string lower_text = ToLower(std::string(text));
  std::size_t pos = 0;
  std::size_t matches = 0;
  while (matches < limit) {
    pos = lower_text.find(needle, pos);
    if (pos == std::string::npos) {
      break;
    }
    if (auto record = RecordAround(text, pos)) {
      std::cout << "match[" << matches << "]:\n";
      PrintRecord(*record, "  ");
      ++matches;
    }
    pos += std::max<std::size_t>(needle.size(), 1);
  }
  if (matches == 0) {
    std::cout << "No shader index record matched '" << needle
              << "'. Runtime 64-bit replay shader hashes are not proven to be "
                 "the same IDs as static container SHA-256 hashes yet.\n";
  }
}

} // namespace

int main(int argc, char **argv) {
  CliOptions cli;
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
    if (arg == "--index") {
      const char *value = require_value("--index");
      if (!value) {
        return 2;
      }
      cli.index_path = value;
    } else if (arg == "--capture") {
      const char *value = require_value("--capture");
      if (!value) {
        return 2;
      }
      cli.capture_path = value;
    } else if (arg == "--summary") {
      cli.show_summary = true;
    } else if (arg == "--list-runtime-shaders") {
      cli.list_runtime_shaders = true;
    } else if (arg == "--match-runtime-shaders") {
      cli.match_runtime_shaders = true;
    } else if (arg == "--top-shaders") {
      const char *value = require_value("--top-shaders");
      if (!value || !ParseSize(value, cli.top_shaders)) {
        std::cerr << "--top-shaders expects an integer\n";
        return 2;
      }
    } else if (arg == "--hash") {
      const char *value = require_value("--hash");
      if (!value) {
        return 2;
      }
      cli.hash = value;
    } else if (arg == "--find") {
      cli.find_hash = true;
    } else if (arg == "--limit") {
      const char *value = require_value("--limit");
      if (!value || !ParseSize(value, cli.limit)) {
        std::cerr << "--limit expects an integer\n";
        return 2;
      }
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 2;
    }
  }

  if (!cli.show_summary && !cli.find_hash && !cli.list_runtime_shaders &&
      !cli.match_runtime_shaders) {
    cli.show_summary = true;
  }
  if (cli.find_hash && cli.hash.empty()) {
    std::cerr << "--find requires --hash <value>\n";
    return 2;
  }
  if ((cli.list_runtime_shaders || cli.match_runtime_shaders) &&
      cli.capture_path.empty()) {
    std::cerr << "--list-runtime-shaders/--match-runtime-shaders require "
                 "--capture <events.jsonl>\n";
    return 2;
  }

  const auto resolved = ResolveIndexPath(cli.index_path);
  if (!resolved) {
    std::cerr << "shader index does not exist: " << cli.index_path.string()
              << "\n";
    return 1;
  }

  std::string text;
  if (!LoadText(*resolved, text)) {
    std::cerr << "could not read shader index: " << resolved->string() << "\n";
    return 1;
  }

  RuntimeShaderCapture runtime_capture;
  if (cli.list_runtime_shaders || cli.match_runtime_shaders) {
    std::string capture_error;
    if (!LoadRuntimeShaderCapture(cli.capture_path, runtime_capture,
                                  capture_error)) {
      std::cerr << capture_error << "\n";
      return 1;
    }
  }

  if (cli.show_summary) {
    PrintSummary(text, *resolved);
  }
  if (cli.find_hash) {
    if (cli.show_summary) {
      std::cout << "\n";
    }
    FindHash(text, cli.hash, cli.limit);
  }
  if (cli.list_runtime_shaders) {
    if (cli.show_summary || cli.find_hash) {
      std::cout << "\n";
    }
    PrintRuntimeShaders(runtime_capture, cli.top_shaders);
  }
  if (cli.match_runtime_shaders) {
    if (cli.show_summary || cli.find_hash || cli.list_runtime_shaders) {
      std::cout << "\n";
    }
    MatchRuntimeShaders(text, runtime_capture, cli.top_shaders);
  }
  return 0;
}
