#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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
};

struct RuntimeShaderPairUsage {
  uint64_t vertex_hash = 0;
  uint64_t pixel_hash = 0;
  uint64_t draw_count = 0;
};

struct RuntimeShaderCapture {
  std::filesystem::path path;
  uint64_t lines = 0;
  uint64_t shader_events = 0;
  uint64_t draw_events = 0;
  std::vector<RuntimeShaderUsage> shaders;
  std::vector<RuntimeShaderPairUsage> pairs;
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

uint64_t ExtractJsonHexU64(std::string_view block, std::string_view key) {
  if (auto value = ParseHexU64(ExtractJsonString(block, key))) {
    return *value;
  }
  return 0;
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
      auto &usage = shaders[{stage, hash}];
      usage.stage = stage;
      usage.hash = hash;
      ++usage.load_count;
      usage.max_dwords = std::max(usage.max_dwords, dwords);
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
            << " shader_pairs=" << capture.pairs.size() << "\n";
  std::cout << "\nRuntime shaders:\n";
  const std::size_t shader_count =
      std::min<std::size_t>(top_count, capture.shaders.size());
  for (std::size_t i = 0; i < shader_count; ++i) {
    const RuntimeShaderUsage &usage = capture.shaders[i];
    std::cout << "  " << StageName(usage.stage) << " 0x" << std::hex
              << std::uppercase << usage.hash << std::dec
              << " draws=" << usage.draw_count
              << " loads=" << usage.load_count
              << " max_dwords=" << usage.max_dwords << "\n";
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
    std::cout << "  " << StageName(usage.stage) << " 0x" << std::hex
              << std::uppercase << usage.hash << std::dec
              << " draws=" << usage.draw_count
              << " loads=" << usage.load_count
              << " exact_substring_match="
              << (pos == std::string::npos ? "no" : "yes") << "\n";
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
