#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CliOptions {
  std::filesystem::path index_path = "shader_work/shaders/index.json";
  bool show_summary = false;
  bool find_hash = false;
  std::string hash;
  std::size_t limit = 8;
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
    } else if (arg == "--summary") {
      cli.show_summary = true;
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

  if (!cli.show_summary && !cli.find_hash) {
    cli.show_summary = true;
  }
  if (cli.find_hash && cli.hash.empty()) {
    std::cerr << "--find requires --hash <value>\n";
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

  if (cli.show_summary) {
    PrintSummary(text, *resolved);
  }
  if (cli.find_hash) {
    if (cli.show_summary) {
      std::cout << "\n";
    }
    FindHash(text, cli.hash, cli.limit);
  }
  return 0;
}
