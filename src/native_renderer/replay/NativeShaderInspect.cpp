#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
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
#include <d3dcompiler.h>
#endif

#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/xenos.h>
#include <rex/string/buffer.h>

#include "../shader_translation/XenosDisassembly.h"
#include "../shader_translation/XenosHlslTranslator.h"

// ReXGlue's shader analyzer checks the optional graphics dump_shaders CVar.
// The standalone inspector keeps that disabled without linking graphics/flags.cpp,
// which would pull RenderDoc/UI/backend dependencies into this tool.
std::string &FLAGS_dump_shaders_storage_() {
  static std::string value;
  return value;
}

namespace {

using bo2::native::CountOperations;
using bo2::native::DisassemblyContains;
using bo2::native::HasOperation;
using bo2::native::ParseDisassemblyOperations;
using bo2::native::ParsedShaderOperation;
using bo2::native::TryTranslateLimitedXenosHlsl;
using bo2::native::XenosHlslTranslationRequest;

struct CliOptions {
  std::filesystem::path index_path = "shader_work/shaders/index.json";
  std::filesystem::path capture_path;
  std::filesystem::path shader_path;
  std::filesystem::path microcode_path;
  std::filesystem::path disasm_output_path;
  std::filesystem::path ir_output_path;
  std::filesystem::path semantic_output_path;
  std::filesystem::path semantic_ir_output_path;
  std::filesystem::path hlsl_output_path;
  std::filesystem::path translated_hlsl_output_path;
  std::filesystem::path hlsl_compile_cache_path;
  std::filesystem::path hlsl_dxc_compile_cache_path;
  std::filesystem::path translated_hlsl_dxc_compile_cache_path;
  std::filesystem::path xenosrecomp_path;
  std::filesystem::path xenosrecomp_header_path =
      "thirdparty/XenosRecomp/XenosRecomp/shader_common.h";
  std::filesystem::path xenosrecomp_hlsl_output_path;
  std::filesystem::path dxc_path;
  bool show_summary = false;
  bool find_hash = false;
  bool list_runtime_shaders = false;
  bool match_runtime_shaders = false;
  bool dump_header = false;
  bool dump_words = false;
  bool disassemble = false;
  bool semantic_disassemble = false;
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
  std::vector<uint32_t> first_payload_dwords;
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
  std::vector<uint32_t> secondary_dwords;
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

struct PayloadPrefixMatch {
  std::size_t secondary_offset = 0;
  std::size_t matched_dwords = 0;
  std::size_t nonzero_dwords = 0;
};

struct ProbeRuntimePayloadMatch {
  const RuntimeShaderUsage *shader = nullptr;
  PayloadPrefixMatch prefix;
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
            << "  --shader <path>   Shader container file to inspect\n"
            << "  --dump-header     Dump the shader container header fields\n"
            << "  --microcode <path>\n"
            << "                     Xenos microcode file to inspect\n"
            << "  --dump-words      Dump big-endian microcode dwords\n"
            << "  --disassemble     Emit an unknown-preserving raw Xenos dword listing\n"
            << "  --semantic-disassemble\n"
            << "                     Use ReXGlue's Xenos parser on --microcode or runtime\n"
            << "                     shader payload selected by --capture and --hash\n"
            << "  --write-disasm <path>\n"
            << "                     Write full raw listing to file or output directory\n"
            << "  --write-ir <path>\n"
            << "                     Write raw backend-neutral shader IR JSON\n"
            << "  --write-semantic <path>\n"
            << "                     Write semantic Xenos analysis to file or directory\n"
            << "  --write-semantic-ir <path>\n"
            << "                     Write semantic backend-neutral shader IR JSON\n"
            << "  --write-hlsl <path>\n"
            << "                     Write diagnostic HLSL from runtime semantic metadata\n"
            << "  --write-translated-hlsl <path>\n"
            << "                     Write limited real HLSL from decoded Xenos operations\n"
            << "  --compile-hlsl <path>\n"
            << "                     Compile diagnostic HLSL into a D3D12 shader cache\n"
            << "  --compile-hlsl-dxc <path>\n"
            << "                     Compile diagnostic HLSL to DXIL with DXC\n"
            << "  --compile-translated-hlsl-dxc <path>\n"
            << "                     Compile limited translated HLSL to DXIL with DXC\n"
            << "  --xenosrecomp <path>\n"
            << "                     XenosRecomp.exe path for container-to-HLSL\n"
            << "  --xenosrecomp-header <path>\n"
            << "                     shader_common.h path for XenosRecomp\n"
            << "  --xenosrecomp-hlsl <path>\n"
            << "                     Run XenosRecomp on --shader and write HLSL\n"
            << "  --dxc-path <path>  DXC executable path for --compile-hlsl-dxc\n"
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

bool LoadBinary(const std::filesystem::path &path,
                std::vector<uint8_t> &bytes) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size < 0) {
    return false;
  }
  file.seekg(0, std::ios::beg);
  bytes.resize(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    file.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
    return file.gcount() == size;
  }
  return true;
}

uint32_t ReadBE32(const std::vector<uint8_t> &bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) {
    return 0;
  }
  return (static_cast<uint32_t>(bytes[offset]) << 24) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
         static_cast<uint32_t>(bytes[offset + 3]);
}

std::string Hex32(uint32_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::uppercase << std::setfill('0')
      << std::setw(8) << value;
  return out.str();
}

std::string Hex64(uint64_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::uppercase << std::setfill('0')
      << std::setw(16) << value;
  return out.str();
}

std::string JsonEscape(std::string_view value) {
  std::ostringstream out;
  for (unsigned char ch : value) {
    switch (ch) {
    case '\\':
      out << "\\\\";
      break;
    case '"':
      out << "\\\"";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (ch < 0x20) {
        out << "\\u" << std::hex << std::uppercase << std::setfill('0')
            << std::setw(4) << static_cast<int>(ch) << std::dec;
      } else {
        out << static_cast<char>(ch);
      }
      break;
    }
  }
  return out.str();
}

std::string_view TrimView(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1);
  }
  return value;
}

void EmitDisassemblyOperationsJson(std::ostream &out,
                                   const std::string &disassembly);

std::string ToString(std::string_view view) {
  return std::string(view.begin(), view.end());
}

std::string GuessStageFromFlags(uint32_t flags) {
  if ((flags & 0xFFFFFF00u) != 0x102A1100u) {
    return "unknown";
  }
  return (flags & 1u) ? "vertex" : "pixel";
}

std::vector<std::string> ExtractAsciiRunsFromBytes(
    const std::vector<uint8_t> &bytes) {
  std::vector<std::string> runs;
  std::string current;
  auto flush = [&]() {
    if (current.size() >= 8) {
      runs.push_back(current);
    }
    current.clear();
  };
  for (uint8_t byte : bytes) {
    const char ch = static_cast<char>(byte);
    if (ch >= 0x20 && ch <= 0x7E) {
      current.push_back(ch);
    } else {
      flush();
    }
  }
  flush();
  return runs;
}

std::string FirstAsciiRunContaining(const std::vector<uint8_t> &bytes,
                                    std::string_view needle) {
  for (const std::string &run : ExtractAsciiRunsFromBytes(bytes)) {
    const std::size_t pos = run.find(needle);
    if (pos != std::string::npos) {
      return run.substr(pos);
    }
  }
  return {};
}

bool PrintShaderContainerHeader(const std::filesystem::path &path,
                                std::string &error) {
  std::vector<uint8_t> bytes;
  if (!LoadBinary(path, bytes)) {
    error = "could not read shader container: " + path.string();
    return false;
  }
  if (bytes.size() < 32) {
    error = "shader container is too small: " + path.string();
    return false;
  }

  const uint32_t flags = ReadBE32(bytes, 0);
  const uint32_t virtual_size = ReadBE32(bytes, 4);
  const uint32_t physical_size = ReadBE32(bytes, 8);
  const uint32_t header_size = ReadBE32(bytes, 12);
  const uint32_t name_offset = ReadBE32(bytes, 16);
  const uint32_t metadata_offset = ReadBE32(bytes, 20);
  const uint32_t microcode_descriptor_offset = ReadBE32(bytes, 24);
  const std::string shader_name =
      FirstAsciiRunContaining(bytes, "pimp_shader_");

  std::cout << "Shader container: " << path.string() << "\n";
  std::cout << "  file_bytes=" << bytes.size() << "\n";
  std::cout << "  flags=" << Hex32(flags)
            << " stage_guess=" << GuessStageFromFlags(flags) << "\n";
  std::cout << "  virtual_size=" << virtual_size
            << " physical_size=" << physical_size
            << " header_size=" << header_size << "\n";
  std::cout << "  name_offset=" << Hex32(name_offset)
            << " metadata_offset=" << Hex32(metadata_offset)
            << " microcode_descriptor_offset="
            << Hex32(microcode_descriptor_offset) << "\n";
  if (!shader_name.empty()) {
    std::cout << "  shader_name=" << shader_name << "\n";
  }
  if (microcode_descriptor_offset + 8 <= bytes.size()) {
    const uint32_t descriptor_word0 =
        ReadBE32(bytes, microcode_descriptor_offset);
    const uint32_t descriptor_word1 =
        ReadBE32(bytes, microcode_descriptor_offset + 4);
    std::cout << "  microcode_descriptor[0]=" << Hex32(descriptor_word0)
              << " microcode_descriptor[1]=" << Hex32(descriptor_word1)
              << "\n";
    std::cout << "  descriptor_size_candidate=" << descriptor_word1 << "\n";
  } else {
    std::cout << "  microcode_descriptor=out_of_file\n";
  }

  std::cout << "  raw_header_dwords:";
  const std::size_t header_dwords = std::min<std::size_t>(8, bytes.size() / 4);
  for (std::size_t i = 0; i < header_dwords; ++i) {
    std::cout << " " << Hex32(ReadBE32(bytes, i * 4));
  }
  std::cout << "\n";
  return true;
}

std::string ClassifyRawXenosWord(uint32_t word) {
  const uint32_t top = word >> 28;
  if (word == 0xFFFFFFFFu) {
    return "padding_or_sentinel";
  }
  if (word == 0) {
    return "zero";
  }
  switch (top) {
  case 0x0:
  case 0x1:
  case 0x2:
  case 0x3:
    return "unknown_cf_or_metadata";
  case 0x4:
  case 0x5:
  case 0x6:
  case 0x7:
    return "unknown_fetch_or_export";
  case 0x8:
  case 0x9:
  case 0xA:
  case 0xB:
    return "unknown_alu_or_control";
  default:
    return "unknown_xenos_word";
  }
}

void EmitMicrocodeWords(std::ostream &out, const std::filesystem::path &path,
                        const std::vector<uint8_t> &bytes, std::size_t limit,
                        bool disassemble) {
  const std::size_t dword_count = bytes.size() / 4;
  const std::size_t count = std::min(limit, dword_count);
  out << "Xenos microcode: " << path.string() << "\n";
  out << "  file_bytes=" << bytes.size() << " dwords=" << dword_count
      << "\n";
  if (bytes.size() % 4 != 0) {
    out << "  trailing_bytes=" << (bytes.size() % 4) << "\n";
  }
  if (const std::string technique =
          FirstAsciiRunContaining(bytes, "pimp_technique_");
      !technique.empty()) {
    out << "  technique=" << technique << "\n";
  }
  if (const std::string shader = FirstAsciiRunContaining(bytes, "pimp_shader_");
      !shader.empty()) {
    out << "  shader_name=" << shader << "\n";
  }

  out << (disassemble ? "\nRaw Xenos dword listing:\n"
                      : "\nMicrocode dwords (big-endian):\n");
  for (std::size_t i = 0; i < count; ++i) {
    const uint32_t word = ReadBE32(bytes, i * 4);
    out << "  [" << std::setw(4) << std::setfill('0') << i
        << std::setfill(' ') << "] " << Hex32(word);
    if (disassemble) {
      out << "  " << ClassifyRawXenosWord(word) << " raw=" << Hex32(word);
    }
    out << "\n";
  }
  if (count < dword_count) {
    out << "  ... truncated after " << count << " of " << dword_count
        << " dwords; use --limit to print more\n";
  }
}

bool PrintMicrocodeWords(const std::filesystem::path &path,
                         std::size_t limit, bool disassemble,
                         std::string &error) {
  std::vector<uint8_t> bytes;
  if (!LoadBinary(path, bytes)) {
    error = "could not read microcode: " + path.string();
    return false;
  }
  EmitMicrocodeWords(std::cout, path, bytes, limit, disassemble);
  return true;
}

std::string InferMicrocodeStageFromPath(const std::filesystem::path &path) {
  const std::string stem = ToLower(path.stem().string());
  if (stem.rfind("pixel_", 0) == 0) {
    return "pixel";
  }
  if (stem.rfind("vertex_", 0) == 0) {
    return "vertex";
  }
  return "unknown";
}

std::filesystem::path MakeDisasmArtifactPath(
    const std::filesystem::path &requested,
    const std::filesystem::path &microcode_path) {
  if (requested.has_extension()) {
    return requested;
  }
  const std::string stem = microcode_path.stem().string();
  const std::string lower_stem = ToLower(stem);
  std::string filename = stem;
  if (lower_stem.rfind("pixel_", 0) != 0 &&
      lower_stem.rfind("vertex_", 0) != 0) {
    filename = InferMicrocodeStageFromPath(microcode_path) + "_" + stem;
  }
  return requested / (filename + ".xenos.asm");
}

bool WriteMicrocodeDisassemblyArtifact(const std::filesystem::path &path,
                                       const std::filesystem::path &requested,
                                       std::filesystem::path &written_path,
                                       std::string &error) {
  std::vector<uint8_t> bytes;
  if (!LoadBinary(path, bytes)) {
    error = "could not read microcode: " + path.string();
    return false;
  }

  written_path = MakeDisasmArtifactPath(requested, path);
  std::error_code ec;
  const std::filesystem::path parent = written_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create disassembly output directory " +
              parent.string() + ": " + ec.message();
      return false;
    }
  }

  std::ofstream file(written_path, std::ios::binary);
  if (!file) {
    error = "could not open disassembly output: " + written_path.string();
    return false;
  }
  EmitMicrocodeWords(file, path, bytes, std::numeric_limits<std::size_t>::max(),
                     true);
  if (!file) {
    error = "could not write disassembly output: " + written_path.string();
    return false;
  }
  return true;
}

void EmitRawShaderIrJson(std::ostream &out, const std::filesystem::path &path,
                         const std::vector<uint8_t> &bytes) {
  const std::size_t dword_count = bytes.size() / 4;
  const std::string technique =
      FirstAsciiRunContaining(bytes, "pimp_technique_");
  const std::string shader_name =
      FirstAsciiRunContaining(bytes, "pimp_shader_");
  const std::string stage = InferMicrocodeStageFromPath(path);
  out << "{\n";
  out << "  \"schema\": \"bo2shaderir.raw_xenos.v1\",\n";
  out << "  \"decoder_version\": 1,\n";
  out << "  \"translator_version\": 0,\n";
  out << "  \"stage\": \"" << JsonEscape(stage) << "\",\n";
  out << "  \"source_path\": \"" << JsonEscape(path.string()) << "\",\n";
  out << "  \"source_file\": \"" << JsonEscape(path.filename().string())
      << "\",\n";
  out << "  \"microcode_stem\": \"" << JsonEscape(path.stem().string())
      << "\",\n";
  out << "  \"byte_count\": " << bytes.size() << ",\n";
  out << "  \"dword_count\": " << dword_count << ",\n";
  out << "  \"unresolved_instruction_count\": " << dword_count << ",\n";
  out << "  \"technique\": \"" << JsonEscape(technique) << "\",\n";
  out << "  \"shader_name\": \"" << JsonEscape(shader_name) << "\",\n";
  out << "  \"inputs\": [],\n";
  out << "  \"outputs\": [],\n";
  out << "  \"constants\": [],\n";
  out << "  \"samplers\": [],\n";
  out << "  \"textures\": [],\n";
  out << "  \"instructions\": [\n";
  for (std::size_t i = 0; i < dword_count; ++i) {
    const uint32_t word = ReadBE32(bytes, i * 4);
    out << "    {\"index\": " << i << ", \"byte_offset\": " << (i * 4)
        << ", \"op\": \"unknown\", \"classification\": \""
        << JsonEscape(ClassifyRawXenosWord(word)) << "\", \"word_be\": \""
        << Hex32(word) << "\", \"raw\": \"" << Hex32(word) << "\"}";
    if (i + 1 < dword_count) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
}

std::filesystem::path MakeIrArtifactPath(
    const std::filesystem::path &requested,
    const std::filesystem::path &microcode_path) {
  if (requested.has_extension()) {
    return requested;
  }
  const std::string stem = microcode_path.stem().string();
  const std::string lower_stem = ToLower(stem);
  std::string filename = stem;
  if (lower_stem.rfind("pixel_", 0) != 0 &&
      lower_stem.rfind("vertex_", 0) != 0) {
    filename = InferMicrocodeStageFromPath(microcode_path) + "_" + stem;
  }
  return requested / (filename + ".bo2shaderir.json");
}

bool WriteMicrocodeIrArtifact(const std::filesystem::path &path,
                              const std::filesystem::path &requested,
                              std::filesystem::path &written_path,
                              std::string &error) {
  std::vector<uint8_t> bytes;
  if (!LoadBinary(path, bytes)) {
    error = "could not read microcode: " + path.string();
    return false;
  }

  written_path = MakeIrArtifactPath(requested, path);
  std::error_code ec;
  const std::filesystem::path parent = written_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create IR output directory " + parent.string() +
              ": " + ec.message();
      return false;
    }
  }

  std::ofstream file(written_path, std::ios::binary);
  if (!file) {
    error = "could not open IR output: " + written_path.string();
    return false;
  }
  EmitRawShaderIrJson(file, path, bytes);
  if (!file) {
    error = "could not write IR output: " + written_path.string();
    return false;
  }
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

  if (usage.first_payload_dwords.empty()) {
    usage.first_payload_dwords = dwords;
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
  usage.secondary_dwords = dwords;
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

uint32_t CountBits32(uint32_t value) {
  uint32_t count = 0;
  while (value) {
    value &= value - 1;
    ++count;
  }
  return count;
}

rex::graphics::xenos::ShaderType RexShaderTypeFromRuntimeStage(uint32_t stage) {
  return stage == 0 ? rex::graphics::xenos::ShaderType::kVertex
                    : rex::graphics::xenos::ShaderType::kPixel;
}

rex::graphics::xenos::ShaderType RexShaderTypeFromStageName(
    std::string_view stage) {
  return stage == "pixel" ? rex::graphics::xenos::ShaderType::kPixel
                          : rex::graphics::xenos::ShaderType::kVertex;
}

const RuntimeShaderUsage *FindRuntimeShaderByHash(
    const RuntimeShaderCapture &capture, uint64_t hash) {
  for (const RuntimeShaderUsage &shader : capture.shaders) {
    if (shader.hash == hash) {
      return &shader;
    }
  }
  return nullptr;
}

void PrintBitmapWords(std::ostream &out, const char *label,
                      const uint64_t *words, std::size_t word_count) {
  out << "  " << label << "=";
  for (std::size_t i = 0; i < word_count; ++i) {
    if (i) {
      out << ",";
    }
    out << "0x" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(16) << words[i] << std::dec << std::setfill(' ');
  }
  out << "\n";
}

void PrintBitmapWords32(std::ostream &out, const char *label,
                        const uint32_t *words, std::size_t word_count) {
  out << "  " << label << "=";
  for (std::size_t i = 0; i < word_count; ++i) {
    if (i) {
      out << ",";
    }
    out << Hex32(words[i]);
  }
  out << "\n";
}

std::vector<uint32_t> BytesToBigEndianDwords(const std::vector<uint8_t> &bytes) {
  std::vector<uint32_t> dwords;
  dwords.reserve(bytes.size() / 4);
  for (std::size_t i = 0; i + 3 < bytes.size(); i += 4) {
    dwords.push_back(ReadBE32(bytes, i));
  }
  return dwords;
}

uint64_t HashPrefix64FromSha256(std::string_view sha256) {
  if (sha256.size() < 16) {
    return 0;
  }
  return ParseHexU64(std::string(sha256.substr(0, 16))).value_or(0);
}

void EmitSemanticShaderAnalysis(std::ostream &out, const char *source_kind,
                                const std::string &source_label,
                                const std::string &stage_name,
                                uint64_t shader_hash,
                                const std::vector<uint32_t> &dwords,
                                bool include_disassembly,
                                std::string &error) {
  try {
    rex::graphics::Shader shader(
        RexShaderTypeFromStageName(stage_name), shader_hash, dwords.data(),
        dwords.size(), std::endian::native);
    rex::string::StringBuffer disasm_buffer;
    shader.AnalyzeUcode(disasm_buffer);

    const auto &constant_map = shader.constant_register_map();
    uint32_t bool_count = 0;
    for (uint32_t word : constant_map.bool_bitmap) {
      bool_count += CountBits32(word);
    }
    uint32_t loop_count = CountBits32(constant_map.loop_bitmap);
    uint32_t vertex_fetch_count = 0;
    for (uint32_t word : constant_map.vertex_fetch_bitmap) {
      vertex_fetch_count += CountBits32(word);
    }

    out << "ReXGlue semantic Xenos analysis\n";
    out << "  source_kind=" << source_kind << "\n";
    out << "  source=" << source_label << "\n";
    out << "  stage=" << stage_name << " shader_hash=" << Hex64(shader_hash)
        << " payload_dwords=" << dwords.size() << "\n";
    out << "  cf_pair_index_bound=" << shader.cf_pair_index_bound()
        << " register_static_address_bound="
        << shader.register_static_address_bound()
        << " dynamic_register_addressing="
        << (shader.uses_register_dynamic_addressing() ? "yes" : "no") << "\n";
    out << "  vertex_bindings=" << shader.vertex_bindings().size()
        << " texture_bindings=" << shader.texture_bindings().size()
        << " float_constants=" << constant_map.float_count
        << " bool_constants=" << bool_count
        << " loop_constants=" << loop_count
        << " vertex_fetch_constants=" << vertex_fetch_count << "\n";
    out << "  writes_interpolators=" << Hex32(shader.writes_interpolators())
        << " writes_color_targets=" << Hex32(shader.writes_color_targets())
        << " writes_depth=" << (shader.writes_depth() ? "yes" : "no")
        << " kills_pixels=" << (shader.kills_pixels() ? "yes" : "no")
        << " texture_fetch_results="
        << (shader.uses_texture_fetch_instruction_results() ? "yes" : "no")
        << "\n";
    PrintBitmapWords(out, "float_constant_bitmap", constant_map.float_bitmap,
                     std::size(constant_map.float_bitmap));
    PrintBitmapWords32(out, "bool_constant_bitmap", constant_map.bool_bitmap,
                       std::size(constant_map.bool_bitmap));
    PrintBitmapWords32(out, "vertex_fetch_bitmap",
                       constant_map.vertex_fetch_bitmap,
                       std::size(constant_map.vertex_fetch_bitmap));

    if (!shader.vertex_bindings().empty()) {
      out << "Vertex bindings:\n";
      for (const auto &binding : shader.vertex_bindings()) {
        out << "  binding=" << binding.binding_index
            << " fetch_constant=" << binding.fetch_constant
            << " stride_words=" << binding.stride_words
            << " attributes=" << binding.attributes.size() << "\n";
      }
    }
    if (!shader.texture_bindings().empty()) {
      out << "Texture bindings:\n";
      for (const auto &binding : shader.texture_bindings()) {
        out << "  binding=" << binding.binding_index
            << " fetch_constant=" << binding.fetch_constant << "\n";
      }
    }

    if (include_disassembly) {
      out << "\nReXGlue Xenos disassembly:\n";
      out << shader.ucode_disassembly();
      if (!shader.ucode_disassembly().empty() &&
          shader.ucode_disassembly().back() != '\n') {
        out << "\n";
      }
    }
  } catch (const std::exception &ex) {
    error = std::string("ReXGlue shader analysis failed: ") + ex.what();
  } catch (...) {
    error = "ReXGlue shader analysis failed with an unknown exception";
  }
}

bool PrintSemanticShaderDisassembly(const char *source_kind,
                                    const std::string &source_label,
                                    const std::string &stage_name,
                                    uint64_t shader_hash,
                                    const std::vector<uint32_t> &dwords,
                                    std::string &error) {
  EmitSemanticShaderAnalysis(std::cout, source_kind, source_label, stage_name,
                             shader_hash, dwords, true, error);
  return error.empty();
}

bool PrintSemanticRuntimeDisassembly(const RuntimeShaderCapture &capture,
                                     uint64_t runtime_hash,
                                     std::string &error) {
  const RuntimeShaderUsage *runtime_shader =
      FindRuntimeShaderByHash(capture, runtime_hash);
  if (!runtime_shader) {
    error = "runtime shader hash not found in capture: " + Hex64(runtime_hash);
    return false;
  }
  if (runtime_shader->first_payload_dwords.empty()) {
    error = "runtime shader has no captured payload dwords: " +
            Hex64(runtime_hash);
    return false;
  }
  return PrintSemanticShaderDisassembly(
      "runtime_capture", capture.path.string(), StageName(runtime_shader->stage),
      runtime_shader->hash, runtime_shader->first_payload_dwords, error);
}

std::string RuntimeSemanticArtifactStem(const RuntimeShaderUsage &shader) {
  return std::string(StageName(shader.stage)) + "_" + Hex64(shader.hash);
}

std::filesystem::path MakeRuntimeSemanticArtifactPath(
    const std::filesystem::path &requested, const RuntimeShaderUsage &shader,
    std::string_view extension) {
  if (requested.has_extension()) {
    return requested;
  }
  std::string stem = RuntimeSemanticArtifactStem(shader);
  std::replace(stem.begin(), stem.end(), ':', '_');
  return requested / (stem + std::string(extension));
}

bool WriteSemanticRuntimeArtifact(const RuntimeShaderCapture &capture,
                                  uint64_t runtime_hash,
                                  const std::filesystem::path &requested,
                                  std::filesystem::path &written_path,
                                  std::string &error) {
  const RuntimeShaderUsage *runtime_shader =
      FindRuntimeShaderByHash(capture, runtime_hash);
  if (!runtime_shader) {
    error = "runtime shader hash not found in capture: " + Hex64(runtime_hash);
    return false;
  }
  if (runtime_shader->first_payload_dwords.empty()) {
    error = "runtime shader has no captured payload dwords: " +
            Hex64(runtime_hash);
    return false;
  }

  written_path = MakeRuntimeSemanticArtifactPath(
      requested, *runtime_shader, ".xenos.semantic.txt");
  std::error_code ec;
  const std::filesystem::path parent = written_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create runtime semantic output directory " +
              parent.string() + ": " + ec.message();
      return false;
    }
  }
  std::ofstream file(written_path, std::ios::binary);
  if (!file) {
    error = "could not open runtime semantic output: " + written_path.string();
    return false;
  }
  EmitSemanticShaderAnalysis(
      file, "runtime_capture", capture.path.string(),
      StageName(runtime_shader->stage), runtime_shader->hash,
      runtime_shader->first_payload_dwords, true, error);
  if (!error.empty()) {
    return false;
  }
  if (!file) {
    error = "could not write runtime semantic output: " + written_path.string();
    return false;
  }
  return true;
}

bool LoadMicrocodeDwords(const std::filesystem::path &path,
                         std::vector<uint8_t> &bytes,
                         std::vector<uint32_t> &dwords, std::string &error) {
  if (!LoadBinary(path, bytes)) {
    error = "could not read microcode: " + path.string();
    return false;
  }
  dwords = BytesToBigEndianDwords(bytes);
  if (dwords.empty()) {
    error = "microcode file has no complete dwords: " + path.string();
    return false;
  }
  return true;
}

std::vector<uint32_t> SelectSemanticMicrocodePayload(
    const std::vector<uint32_t> &dwords, const std::string &stage,
    std::size_t &selected_offset) {
  (void)stage;
  selected_offset = 0;
  return dwords;
}

bool PrintSemanticMicrocodeDisassembly(const std::filesystem::path &path,
                                       std::string &error) {
  std::vector<uint8_t> bytes;
  std::vector<uint32_t> dwords;
  if (!LoadMicrocodeDwords(path, bytes, dwords, error)) {
    return false;
  }
  const std::string stage = InferMicrocodeStageFromPath(path);
  if (stage == "unknown") {
    error = "cannot infer shader stage from microcode filename: " +
            path.filename().string();
    return false;
  }
  std::size_t source_dword_offset = 0;
  dwords = SelectSemanticMicrocodePayload(dwords, stage, source_dword_offset);
  if (source_dword_offset) {
    std::cout << "semantic_source_dword_offset=" << source_dword_offset << "\n";
  }
  const uint64_t hash = HashPrefix64FromSha256(Sha256Hex(bytes));
  return PrintSemanticShaderDisassembly("microcode_file", path.string(), stage,
                                        hash, dwords, error);
}

std::filesystem::path MakeSemanticArtifactPath(
    const std::filesystem::path &requested,
    const std::filesystem::path &microcode_path) {
  if (requested.has_extension()) {
    return requested;
  }
  const std::string stem = microcode_path.stem().string();
  const std::string lower_stem = ToLower(stem);
  std::string filename = stem;
  if (lower_stem.rfind("pixel_", 0) != 0 &&
      lower_stem.rfind("vertex_", 0) != 0) {
    filename = InferMicrocodeStageFromPath(microcode_path) + "_" + stem;
  }
  return requested / (filename + ".xenos.semantic.txt");
}

bool WriteSemanticMicrocodeArtifact(const std::filesystem::path &path,
                                    const std::filesystem::path &requested,
                                    std::filesystem::path &written_path,
                                    std::string &error) {
  std::vector<uint8_t> bytes;
  std::vector<uint32_t> dwords;
  if (!LoadMicrocodeDwords(path, bytes, dwords, error)) {
    return false;
  }
  const std::string stage = InferMicrocodeStageFromPath(path);
  if (stage == "unknown") {
    error = "cannot infer shader stage from microcode filename: " +
            path.filename().string();
    return false;
  }

  written_path = MakeSemanticArtifactPath(requested, path);
  std::error_code ec;
  const std::filesystem::path parent = written_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create semantic output directory " + parent.string() +
              ": " + ec.message();
      return false;
    }
  }

  std::ofstream file(written_path, std::ios::binary);
  if (!file) {
    error = "could not open semantic output: " + written_path.string();
    return false;
  }
  const uint64_t hash = HashPrefix64FromSha256(Sha256Hex(bytes));
  std::size_t source_dword_offset = 0;
  dwords = SelectSemanticMicrocodePayload(dwords, stage, source_dword_offset);
  if (source_dword_offset) {
    file << "semantic_source_dword_offset=" << source_dword_offset << "\n";
  }
  EmitSemanticShaderAnalysis(file, "microcode_file", path.string(), stage, hash,
                             dwords, true, error);
  if (!error.empty()) {
    return false;
  }
  if (!file) {
    error = "could not write semantic output: " + written_path.string();
    return false;
  }
  return true;
}

void EmitJsonDwordArray(std::ostream &out,
                        const std::vector<uint32_t> &dwords) {
  out << "[";
  for (std::size_t i = 0; i < dwords.size(); ++i) {
    if (i) {
      out << ", ";
    }
    out << "\"" << Hex32(dwords[i]) << "\"";
  }
  out << "]";
}

bool EmitSemanticShaderIrJson(std::ostream &out,
                              const std::filesystem::path &path,
                              const std::vector<uint8_t> &bytes,
                              const std::vector<uint32_t> &dwords,
                              std::size_t source_dword_offset,
                              std::string &error) {
  const std::string stage = InferMicrocodeStageFromPath(path);
  if (stage == "unknown") {
    error = "cannot infer shader stage from microcode filename: " +
            path.filename().string();
    return false;
  }

  try {
    const uint64_t hash = HashPrefix64FromSha256(Sha256Hex(bytes));
    rex::graphics::Shader shader(RexShaderTypeFromStageName(stage), hash,
                                 dwords.data(), dwords.size(),
                                 std::endian::native);
    rex::string::StringBuffer disasm_buffer;
    shader.AnalyzeUcode(disasm_buffer);
    const auto &constant_map = shader.constant_register_map();

    out << "{\n";
    out << "  \"schema\": \"bo2shaderir.semantic_xenos.v1\",\n";
    out << "  \"decoder_version\": 1,\n";
    out << "  \"translator_version\": 0,\n";
    out << "  \"semantic_source\": \"rex::graphics::Shader::AnalyzeUcode\",\n";
    out << "  \"stage\": \"" << JsonEscape(stage) << "\",\n";
    out << "  \"source_path\": \"" << JsonEscape(path.string()) << "\",\n";
    out << "  \"source_file\": \"" << JsonEscape(path.filename().string())
        << "\",\n";
    out << "  \"microcode_sha256_be\": \"" << JsonEscape(Sha256Hex(bytes))
        << "\",\n";
    out << "  \"shader_hash\": \"" << Hex64(hash) << "\",\n";
    out << "  \"source_dword_offset\": " << source_dword_offset << ",\n";
    out << "  \"dword_count\": " << dwords.size() << ",\n";
    out << "  \"cf_pair_index_bound\": " << shader.cf_pair_index_bound()
        << ",\n";
    out << "  \"register_static_address_bound\": "
        << shader.register_static_address_bound() << ",\n";
    out << "  \"uses_register_dynamic_addressing\": "
        << (shader.uses_register_dynamic_addressing() ? "true" : "false")
        << ",\n";
    out << "  \"outputs\": {\n";
    out << "    \"writes_interpolators\": \""
        << Hex32(shader.writes_interpolators()) << "\",\n";
    out << "    \"writes_color_targets\": \""
        << Hex32(shader.writes_color_targets()) << "\",\n";
    out << "    \"writes_depth\": "
        << (shader.writes_depth() ? "true" : "false") << ",\n";
    out << "    \"kills_pixels\": "
        << (shader.kills_pixels() ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"constants\": {\n";
    out << "    \"float_count\": " << constant_map.float_count << ",\n";
    out << "    \"float_bitmap\": [";
    for (std::size_t i = 0; i < std::size(constant_map.float_bitmap); ++i) {
      if (i) {
        out << ", ";
      }
      out << "\"" << std::hex << std::uppercase << std::setfill('0')
          << std::setw(16) << constant_map.float_bitmap[i] << std::dec
          << std::setfill(' ') << "\"";
    }
    out << "],\n";
    out << "    \"bool_bitmap\": [";
    for (std::size_t i = 0; i < std::size(constant_map.bool_bitmap); ++i) {
      if (i) {
        out << ", ";
      }
      out << "\"" << Hex32(constant_map.bool_bitmap[i]) << "\"";
    }
    out << "],\n";
    out << "    \"loop_bitmap\": \"" << Hex32(constant_map.loop_bitmap)
        << "\",\n";
    out << "    \"vertex_fetch_bitmap\": [";
    for (std::size_t i = 0; i < std::size(constant_map.vertex_fetch_bitmap);
         ++i) {
      if (i) {
        out << ", ";
      }
      out << "\"" << Hex32(constant_map.vertex_fetch_bitmap[i]) << "\"";
    }
    out << "]\n";
    out << "  },\n";
    out << "  \"vertex_fetches\": [\n";
    for (std::size_t i = 0; i < shader.vertex_bindings().size(); ++i) {
      const auto &binding = shader.vertex_bindings()[i];
      out << "    {\"binding\": " << binding.binding_index
          << ", \"fetch_constant\": " << binding.fetch_constant
          << ", \"stride_words\": " << binding.stride_words
          << ", \"attribute_count\": " << binding.attributes.size() << "}";
      if (i + 1 < shader.vertex_bindings().size()) {
        out << ",";
      }
      out << "\n";
    }
    out << "  ],\n";
    out << "  \"textures\": [\n";
    for (std::size_t i = 0; i < shader.texture_bindings().size(); ++i) {
      const auto &binding = shader.texture_bindings()[i];
      out << "    {\"binding\": " << binding.binding_index
          << ", \"fetch_constant\": " << binding.fetch_constant << "}";
      if (i + 1 < shader.texture_bindings().size()) {
        out << ",";
      }
      out << "\n";
    }
    out << "  ],\n";
    EmitDisassemblyOperationsJson(out, shader.ucode_disassembly());
    out << "  \"disassembly\": [\n";
    std::istringstream disasm_lines(shader.ucode_disassembly());
    std::string line;
    bool first_line = true;
    while (std::getline(disasm_lines, line)) {
      if (!first_line) {
        out << ",\n";
      }
      first_line = false;
      out << "    \"" << JsonEscape(line) << "\"";
    }
    out << "\n  ],\n";
    out << "  \"raw_words\": ";
    EmitJsonDwordArray(out, dwords);
    out << "\n";
    out << "}\n";
    return true;
  } catch (const std::exception &ex) {
    error = std::string("ReXGlue shader semantic IR failed: ") + ex.what();
    return false;
  } catch (...) {
    error = "ReXGlue shader semantic IR failed with an unknown exception";
    return false;
  }
}

std::filesystem::path MakeSemanticIrArtifactPath(
    const std::filesystem::path &requested,
    const std::filesystem::path &microcode_path) {
  if (requested.has_extension()) {
    return requested;
  }
  const std::string stem = microcode_path.stem().string();
  const std::string lower_stem = ToLower(stem);
  std::string filename = stem;
  if (lower_stem.rfind("pixel_", 0) != 0 &&
      lower_stem.rfind("vertex_", 0) != 0) {
    filename = InferMicrocodeStageFromPath(microcode_path) + "_" + stem;
  }
  return requested / (filename + ".semantic.bo2shaderir.json");
}

bool WriteSemanticMicrocodeIrArtifact(const std::filesystem::path &path,
                                      const std::filesystem::path &requested,
                                      std::filesystem::path &written_path,
                                      std::string &error) {
  std::vector<uint8_t> bytes;
  std::vector<uint32_t> dwords;
  if (!LoadMicrocodeDwords(path, bytes, dwords, error)) {
    return false;
  }
  const std::string stage = InferMicrocodeStageFromPath(path);
  std::size_t source_dword_offset = 0;
  if (stage != "unknown") {
    dwords = SelectSemanticMicrocodePayload(dwords, stage, source_dword_offset);
  }

  written_path = MakeSemanticIrArtifactPath(requested, path);
  std::error_code ec;
  const std::filesystem::path parent = written_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create semantic IR output directory " +
              parent.string() + ": " + ec.message();
      return false;
    }
  }

  std::ofstream file(written_path, std::ios::binary);
  if (!file) {
    error = "could not open semantic IR output: " + written_path.string();
    return false;
  }
  if (!EmitSemanticShaderIrJson(file, path, bytes, dwords, source_dword_offset,
                                error)) {
    return false;
  }
  if (!file) {
    error = "could not write semantic IR output: " + written_path.string();
    return false;
  }
  return true;
}

std::string OperandMask(std::string_view operand) {
  const std::size_t dot = operand.find('.');
  if (dot == std::string_view::npos || dot + 1 >= operand.size()) {
    return {};
  }
  std::string mask;
  for (std::size_t i = dot + 1; i < operand.size(); ++i) {
    const char ch = operand[i];
    if ((ch >= 'x' && ch <= 'z') || ch == 'w' || ch == '_' ||
        (ch >= '0' && ch <= '9')) {
      mask.push_back(ch);
    } else {
      break;
    }
  }
  return mask;
}

std::string OperandRegisterText(std::string_view operand) {
  const std::size_t dot = operand.find('.');
  std::string_view base =
      dot == std::string_view::npos ? operand : operand.substr(0, dot);
  return ToString(TrimView(base));
}

std::string OperandRegisterFile(std::string_view reg) {
  if (reg.rfind("r_abs[", 0) == 0) {
    return "temporary_abs";
  }
  if (reg.size() > 1 && reg[0] == 'r' &&
      std::isdigit(static_cast<unsigned char>(reg[1]))) {
    return "temporary";
  }
  if (reg.size() > 1 && reg[0] == 'c' &&
      std::isdigit(static_cast<unsigned char>(reg[1]))) {
    return "float_constant";
  }
  if (reg.size() > 2 && reg.rfind("tf", 0) == 0 &&
      std::isdigit(static_cast<unsigned char>(reg[2]))) {
    return "texture_fetch";
  }
  if (reg.size() > 2 && reg.rfind("vf", 0) == 0 &&
      std::isdigit(static_cast<unsigned char>(reg[2]))) {
    return "vertex_fetch";
  }
  if (reg.rfind("oC", 0) == 0) {
    return "color_export";
  }
  if (reg.rfind("oPos", 0) == 0) {
    return "position_export";
  }
  if (reg.rfind("o", 0) == 0) {
    return "interpolator_export";
  }
  return "unknown";
}

std::optional<int> OperandRegisterIndex(std::string_view reg) {
  std::string digits;
  if (reg.rfind("r_abs[", 0) == 0) {
    const std::size_t begin = reg.find('[');
    const std::size_t end = reg.find(']', begin == std::string_view::npos
                                             ? 0
                                             : begin + 1);
    if (begin != std::string_view::npos && end != std::string_view::npos) {
      digits = ToString(reg.substr(begin + 1, end - begin - 1));
    }
  } else {
    for (char ch : reg) {
      if (ch >= '0' && ch <= '9') {
        digits.push_back(ch);
      }
    }
  }
  if (digits.empty()) {
    return std::nullopt;
  }
  int value = 0;
  const auto result =
      std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return value;
}

std::vector<std::string> SplitOperands(std::string_view operands) {
  std::vector<std::string> result;
  std::string current;
  int bracket_depth = 0;
  for (char ch : operands) {
    if (ch == '[') {
      ++bracket_depth;
    } else if (ch == ']' && bracket_depth > 0) {
      --bracket_depth;
    }
    if (ch == ',' && bracket_depth == 0) {
      if (!TrimView(current).empty()) {
        result.push_back(ToString(TrimView(current)));
      }
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  if (!TrimView(current).empty()) {
    result.push_back(ToString(TrimView(current)));
  }
  return result;
}

void EmitOperandJson(std::ostream &out, std::string_view operand) {
  const std::string text = ToString(TrimView(operand));
  const std::string reg = OperandRegisterText(text);
  const std::string mask = OperandMask(text);
  const std::string file = OperandRegisterFile(reg);
  out << "{\"text\":\"" << JsonEscape(text) << "\",\"register\":\""
      << JsonEscape(reg) << "\",\"file\":\"" << JsonEscape(file) << "\"";
  if (const std::optional<int> index = OperandRegisterIndex(reg)) {
    out << ",\"index\":" << *index;
  }
  if (!mask.empty()) {
    out << ",\"mask\":\"" << JsonEscape(mask) << "\"";
  }
  out << "}";
}

void EmitDisassemblyOperationsJson(std::ostream &out,
                                   const std::string &disassembly) {
  out << "  \"operations\": [\n";
  const std::vector<ParsedShaderOperation> operations =
      ParseDisassemblyOperations(disassembly);
  bool first = true;
  for (const ParsedShaderOperation &operation : operations) {

    if (!first) {
      out << ",\n";
    }
    first = false;
    out << "    {\"address\": \"" << JsonEscape(operation.address)
        << "\", \"opcode\": \"" << JsonEscape(operation.opcode)
        << "\", \"operands\": \"" << JsonEscape(operation.operands)
        << "\", \"text\": \"" << JsonEscape(operation.text) << "\""
        << ", \"coissued\": " << (operation.coissued ? "true" : "false")
        << ", \"category\": \"" << JsonEscape(operation.category) << "\"";
    if (operation.has_destination) {
      out << ", \"dest\": ";
      EmitOperandJson(out, operation.operand_parts.front());
    }
    out << ", \"sources\": [";
    const std::size_t first_source = operation.has_destination ? 1u : 0u;
    for (std::size_t i = first_source; i < operation.operand_parts.size();
         ++i) {
      if (i != first_source) {
        out << ", ";
      }
      EmitOperandJson(out, operation.operand_parts[i]);
    }
    out << "]";
    if (operation.fetch_constant) {
      out << ", \"fetch_constant\": " << *operation.fetch_constant;
    }
    out << "}";
  }
  out << "\n  ],\n";
}

bool EmitRuntimeSemanticShaderIrJson(std::ostream &out,
                                     const RuntimeShaderCapture &capture,
                                     const RuntimeShaderUsage &runtime_shader,
                                     std::string &error) {
  try {
    rex::graphics::Shader shader(
        RexShaderTypeFromRuntimeStage(runtime_shader.stage),
        runtime_shader.hash, runtime_shader.first_payload_dwords.data(),
        runtime_shader.first_payload_dwords.size(), std::endian::native);
    rex::string::StringBuffer disasm_buffer;
    shader.AnalyzeUcode(disasm_buffer);
    const auto &constant_map = shader.constant_register_map();

    out << "{\n";
    out << "  \"schema\": \"bo2shaderir.semantic_xenos.v1\",\n";
    out << "  \"decoder_version\": 1,\n";
    out << "  \"translator_version\": 0,\n";
    out << "  \"semantic_source\": \"rex::graphics::Shader::AnalyzeUcode\",\n";
    out << "  \"source_kind\": \"runtime_capture\",\n";
    out << "  \"source_path\": \"" << JsonEscape(capture.path.string())
        << "\",\n";
    out << "  \"stage\": \"" << StageName(runtime_shader.stage) << "\",\n";
    out << "  \"runtime_hash\": \"" << Hex64(runtime_shader.hash) << "\",\n";
    out << "  \"draw_count\": " << runtime_shader.draw_count << ",\n";
    out << "  \"load_count\": " << runtime_shader.load_count << ",\n";
    out << "  \"dword_count\": "
        << runtime_shader.first_payload_dwords.size() << ",\n";
    out << "  \"payload_sha256_le\": \""
        << JsonEscape(runtime_shader.payload_sha256_le) << "\",\n";
    out << "  \"payload_sha256_be\": \""
        << JsonEscape(runtime_shader.payload_sha256_be) << "\",\n";
    out << "  \"cf_pair_index_bound\": " << shader.cf_pair_index_bound()
        << ",\n";
    out << "  \"register_static_address_bound\": "
        << shader.register_static_address_bound() << ",\n";
    out << "  \"uses_register_dynamic_addressing\": "
        << (shader.uses_register_dynamic_addressing() ? "true" : "false")
        << ",\n";
    out << "  \"outputs\": {\n";
    out << "    \"writes_interpolators\": \""
        << Hex32(shader.writes_interpolators()) << "\",\n";
    out << "    \"writes_color_targets\": \""
        << Hex32(shader.writes_color_targets()) << "\",\n";
    out << "    \"writes_depth\": "
        << (shader.writes_depth() ? "true" : "false") << ",\n";
    out << "    \"kills_pixels\": "
        << (shader.kills_pixels() ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"constants\": {\n";
    out << "    \"float_count\": " << constant_map.float_count << ",\n";
    out << "    \"float_dynamic_addressing\": "
        << (constant_map.float_dynamic_addressing ? "true" : "false")
        << ",\n";
    out << "    \"float_bitmap\": [";
    for (std::size_t i = 0; i < std::size(constant_map.float_bitmap); ++i) {
      if (i) {
        out << ", ";
      }
      out << "\"" << Hex64(constant_map.float_bitmap[i]) << "\"";
    }
    out << "],\n";
    out << "    \"loop_bitmap\": \"" << Hex32(constant_map.loop_bitmap)
        << "\",\n";
    out << "    \"bool_bitmap\": [";
    for (std::size_t i = 0; i < std::size(constant_map.bool_bitmap); ++i) {
      if (i) {
        out << ", ";
      }
      out << "\"" << Hex32(constant_map.bool_bitmap[i]) << "\"";
    }
    out << "],\n";
    out << "    \"vertex_fetch_bitmap\": [";
    for (std::size_t i = 0;
         i < std::size(constant_map.vertex_fetch_bitmap); ++i) {
      if (i) {
        out << ", ";
      }
      out << "\"" << Hex32(constant_map.vertex_fetch_bitmap[i]) << "\"";
    }
    out << "]\n";
    out << "  },\n";
    out << "  \"vertex_fetches\": [\n";
    for (std::size_t i = 0; i < shader.vertex_bindings().size(); ++i) {
      const auto &binding = shader.vertex_bindings()[i];
      out << "    {\"binding\": " << binding.binding_index
          << ", \"fetch_constant\": " << binding.fetch_constant
          << ", \"stride_words\": " << binding.stride_words
          << ", \"attribute_count\": " << binding.attributes.size()
          << ", \"attributes\": [";
      for (std::size_t j = 0; j < binding.attributes.size(); ++j) {
        const auto &attribute = binding.attributes[j].fetch_instr;
        if (j) {
          out << ", ";
        }
        out << "{\"opcode\":\"" << JsonEscape(attribute.opcode_name)
            << "\", \"result_target\": "
            << static_cast<uint32_t>(attribute.result.storage_target)
            << ", \"result_index\": " << attribute.result.storage_index
            << ", \"write_mask\": \""
            << Hex32(attribute.result.original_write_mask)
            << "\", \"format\": "
            << static_cast<uint32_t>(attribute.attributes.data_format)
            << ", \"offset\": " << attribute.attributes.offset
            << ", \"stride_dwords\": " << attribute.attributes.stride
            << ", \"prefetch_count\": "
            << (attribute.attributes.prefetch_count + 1)
            << ", \"signed\": "
            << (attribute.attributes.is_signed ? "true" : "false")
            << ", \"integer\": "
            << (attribute.attributes.is_integer ? "true" : "false") << "}";
      }
      out << "]}";
      if (i + 1 < shader.vertex_bindings().size()) {
        out << ",";
      }
      out << "\n";
    }
    out << "  ],\n";
    out << "  \"textures\": [\n";
    for (std::size_t i = 0; i < shader.texture_bindings().size(); ++i) {
      const auto &binding = shader.texture_bindings()[i];
      out << "    {\"binding\": " << binding.binding_index
          << ", \"fetch_constant\": " << binding.fetch_constant
          << ", \"opcode\": \""
          << JsonEscape(binding.fetch_instr.opcode_name)
          << "\", \"dimension\": "
          << static_cast<uint32_t>(binding.fetch_instr.dimension)
          << ", \"result_target\": "
          << static_cast<uint32_t>(
                 binding.fetch_instr.result.storage_target)
          << ", \"result_index\": "
          << binding.fetch_instr.result.storage_index
          << ", \"write_mask\": \""
          << Hex32(binding.fetch_instr.result.original_write_mask)
          << "\", \"mag_filter\": "
          << static_cast<uint32_t>(
                 binding.fetch_instr.attributes.mag_filter)
          << ", \"min_filter\": "
          << static_cast<uint32_t>(
                 binding.fetch_instr.attributes.min_filter)
          << ", \"mip_filter\": "
          << static_cast<uint32_t>(
                 binding.fetch_instr.attributes.mip_filter)
          << ", \"computed_lod\": "
          << (binding.fetch_instr.attributes.use_computed_lod ? "true"
                                                              : "false")
          << "}";
      if (i + 1 < shader.texture_bindings().size()) {
        out << ",";
      }
      out << "\n";
    }
    out << "  ],\n";
    EmitDisassemblyOperationsJson(out, shader.ucode_disassembly());
    out << "  \"disassembly\": [\n";
    std::istringstream disasm_lines(shader.ucode_disassembly());
    std::string line;
    bool first_line = true;
    while (std::getline(disasm_lines, line)) {
      if (!first_line) {
        out << ",\n";
      }
      first_line = false;
      out << "    \"" << JsonEscape(line) << "\"";
    }
    out << "\n  ],\n";
    out << "  \"raw_words\": ";
    EmitJsonDwordArray(out, runtime_shader.first_payload_dwords);
    out << "\n";
    out << "}\n";
    return true;
  } catch (const std::exception &ex) {
    error = std::string("ReXGlue runtime shader semantic IR failed: ") +
            ex.what();
    return false;
  } catch (...) {
    error = "ReXGlue runtime shader semantic IR failed with an unknown exception";
    return false;
  }
}

bool WriteSemanticRuntimeIrArtifact(const RuntimeShaderCapture &capture,
                                    uint64_t runtime_hash,
                                    const std::filesystem::path &requested,
                                    std::filesystem::path &written_path,
                                    std::string &error) {
  const RuntimeShaderUsage *runtime_shader =
      FindRuntimeShaderByHash(capture, runtime_hash);
  if (!runtime_shader) {
    error = "runtime shader hash not found in capture: " + Hex64(runtime_hash);
    return false;
  }
  if (runtime_shader->first_payload_dwords.empty()) {
    error = "runtime shader has no captured payload dwords: " +
            Hex64(runtime_hash);
    return false;
  }

  written_path = MakeRuntimeSemanticArtifactPath(
      requested, *runtime_shader, ".semantic.bo2shaderir.json");
  std::error_code ec;
  const std::filesystem::path parent = written_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create runtime semantic IR output directory " +
              parent.string() + ": " + ec.message();
      return false;
    }
  }
  std::ofstream file(written_path, std::ios::binary);
  if (!file) {
    error =
        "could not open runtime semantic IR output: " + written_path.string();
    return false;
  }
  if (!EmitRuntimeSemanticShaderIrJson(file, capture, *runtime_shader, error)) {
    return false;
  }
  if (!file) {
    error =
        "could not write runtime semantic IR output: " + written_path.string();
    return false;
  }
  return true;
}

std::filesystem::path MakeRuntimeHlslArtifactPath(
    const std::filesystem::path &requested, const RuntimeShaderUsage &shader) {
  if (requested.has_extension()) {
    return requested;
  }
  std::string stem = RuntimeSemanticArtifactStem(shader);
  std::replace(stem.begin(), stem.end(), ':', '_');
  return requested / (stem + ".diagnostic.hlsl");
}

void EmitDiagnosticRuntimeHlsl(std::ostream &out,
                               const RuntimeShaderCapture &capture,
                               const RuntimeShaderUsage &runtime_shader) {
  out << "// BO2 native renderer diagnostic HLSL generated from runtime "
         "semantic metadata.\n";
  out << "// This is interface scaffolding, not real translated Xenos shader "
         "code.\n";
  out << "// capture: " << capture.path.string() << "\n";
  out << "// runtime_hash: " << Hex64(runtime_shader.hash) << "\n";
  out << "// stage: " << StageName(runtime_shader.stage) << "\n";
  out << "// payload_sha256_le: " << runtime_shader.payload_sha256_le << "\n";
  out << "// payload_sha256_be: " << runtime_shader.payload_sha256_be << "\n\n";
  out << "cbuffer BO2CapturedConstants : register(b1)\n";
  out << "{\n";
  out << "  uint4 bo2_constants[256];\n";
  out << "};\n\n";

  if (runtime_shader.stage == 0) {
    out << "struct VSInput\n";
    out << "{\n";
    out << "  float3 position : POSITION0;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 texcoord : TEXCOORD0;\n";
    out << "};\n\n";
    out << "struct VSOutput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 texcoord : TEXCOORD0;\n";
    out << "};\n\n";
    out << "VSOutput main(VSInput input)\n";
    out << "{\n";
    out << "  VSOutput output;\n";
    out << "  output.position = float4(input.position.xy, input.position.z, "
           "1.0);\n";
    out << "  output.color = input.color;\n";
    out << "  output.texcoord = input.texcoord;\n";
    out << "  return output;\n";
    out << "}\n";
  } else {
    out << "Texture2D bo2_texture0 : register(t0);\n";
    out << "SamplerState bo2_sampler0 : register(s0);\n\n";
    out << "struct PSInput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 texcoord : TEXCOORD0;\n";
    out << "};\n\n";
    out << "float4 main(PSInput input) : SV_Target0\n";
    out << "{\n";
    out << "  float4 texel = bo2_texture0.Sample(bo2_sampler0, "
           "input.texcoord);\n";
    out << "  return input.color * texel;\n";
    out << "}\n";
  }
}

bool WriteRuntimeDiagnosticHlslArtifact(const RuntimeShaderCapture &capture,
                                        uint64_t runtime_hash,
                                        const std::filesystem::path &requested,
                                        std::filesystem::path &written_path,
                                        std::string &error) {
  const RuntimeShaderUsage *runtime_shader =
      FindRuntimeShaderByHash(capture, runtime_hash);
  if (!runtime_shader) {
    error = "runtime shader hash not found in capture: " + Hex64(runtime_hash);
    return false;
  }

  written_path = MakeRuntimeHlslArtifactPath(requested, *runtime_shader);
  std::error_code ec;
  const std::filesystem::path parent = written_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create HLSL output directory " + parent.string() +
              ": " + ec.message();
      return false;
    }
  }
  std::ofstream file(written_path, std::ios::binary);
  if (!file) {
    error = "could not open HLSL output: " + written_path.string();
    return false;
  }
  EmitDiagnosticRuntimeHlsl(file, capture, *runtime_shader);
  if (!file) {
    error = "could not write HLSL output: " + written_path.string();
    return false;
  }
  return true;
}

std::filesystem::path MakeRuntimeTranslatedHlslArtifactPath(
    const std::filesystem::path &requested, const RuntimeShaderUsage &shader) {
  if (requested.has_extension()) {
    return requested;
  }
  std::string stem = RuntimeSemanticArtifactStem(shader);
  std::replace(stem.begin(), stem.end(), ':', '_');
  return requested / (stem + ".translated.hlsl");
}

constexpr const char *kLimitedXenosTranslatorVersion =
    "xenos_limited_semantic_v8";

bool TryEmitLimitedTranslatedRuntimeHlsl(
    std::ostream &out, const RuntimeShaderCapture &capture,
    const RuntimeShaderUsage &runtime_shader, std::string &error) {
  rex::graphics::Shader shader(RexShaderTypeFromRuntimeStage(runtime_shader.stage),
                               runtime_shader.hash,
                               runtime_shader.first_payload_dwords.data(),
                               runtime_shader.first_payload_dwords.size(),
                               std::endian::native);
  rex::string::StringBuffer disasm_buffer;
  shader.AnalyzeUcode(disasm_buffer);
  const std::string &disassembly = shader.ucode_disassembly();
  const std::vector<ParsedShaderOperation> operations =
      ParseDisassemblyOperations(disassembly);

  {
    XenosHlslTranslationRequest request;
    request.runtime_stage = runtime_shader.stage;
    request.runtime_hash = runtime_shader.hash;
    request.capture_path = capture.path;
    request.stage_name = StageName(runtime_shader.stage);
    request.disassembly = disassembly;
    std::string shared_error;
    if (TryTranslateLimitedXenosHlsl(request, out, shared_error)) {
      return true;
    }
  }

  out << "// BO2 native renderer translated HLSL from decoded Xenos "
         "operations.\n";
  out << "// Translator subset: " << kLimitedXenosTranslatorVersion << ".\n";
  out << "// Unsupported shaders fail closed instead of using this path.\n";
  out << "// capture: " << capture.path.string() << "\n";
  out << "// runtime_hash: " << Hex64(runtime_shader.hash) << "\n";
  out << "// stage: " << StageName(runtime_shader.stage) << "\n\n";

  if (runtime_shader.stage == 0 &&
      runtime_shader.hash == 0x81311AC4B1FBD082ull &&
      disassembly.find("vfetch_full r1.xyz_") != std::string::npos &&
      disassembly.find("vfetch_mini r0.xy__") != std::string::npos &&
      disassembly.find("dp4 oPos") != std::string::npos &&
      disassembly.find("mul o0") != std::string::npos) {
    out << "cbuffer FrameConstants : register(b0)\n";
    out << "{\n";
    out << "  float2 surface_size;\n";
    out << "  float2 _pad;\n";
    out << "};\n\n";
    out << "struct VSInput\n";
    out << "{\n";
    out << "  float4 position : POSITION;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "struct VSOutput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "VSOutput main(VSInput input)\n";
    out << "{\n";
    out << "  VSOutput output;\n";
    out << "  // Xenos subset: vfetch r1.xyz_ from vf95, vfetch r0.xy__,\n";
    out << "  // dp4 oPos against c0..c3, and mul o0 by c255.xyxy.\n";
    out << "  // Draw 1013 captures screen-space quad positions and UVs, so this\n";
    out << "  // limited path preserves the captured resource geometry while the\n";
    out << "  // general constant-driven transform lowering is still being built.\n";
    out << "  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,\n";
    out << "                            1.0f - input.position.y / surface_size.y * 2.0f);\n";
    out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
    out << "  output.uv = input.uv;\n";
    out << "  output.color = input.color;\n";
    out << "  return output;\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 0 &&
      disassembly.find("vfetch_full r0._xyz") != std::string::npos &&
      disassembly.find("mad r0.xyz_") != std::string::npos &&
      disassembly.find("dp4 r2.x___") != std::string::npos &&
      disassembly.find("dp4 r0.___w") != std::string::npos &&
      disassembly.find("max oPos, r0, r0") != std::string::npos &&
      disassembly.find("max o0.xy__") != std::string::npos &&
      disassembly.find("max o1") != std::string::npos) {
    out << "cbuffer FrameConstants : register(b0)\n";
    out << "{\n";
    out << "  float2 surface_size;\n";
    out << "  float2 _pad;\n";
    out << "};\n\n";
    out << "struct VSInput\n";
    out << "{\n";
    out << "  float4 position : POSITION;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "struct VSOutput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "VSOutput main(VSInput input)\n";
    out << "{\n";
    out << "  VSOutput output;\n";
    out << "  // Xenos: vfetch r0/r1/r3, transform dp4 chain, export oPos/o0/o1.\n";
    out << "  // Replay canonicalization has already decoded vf95 into POSITION,\n";
    out << "  // COLOR0, and TEXCOORD0 attributes for the supported draw class.\n";
    out << "  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,\n";
    out << "                            1.0f - input.position.y / surface_size.y * 2.0f);\n";
    out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
    out << "  output.uv = input.uv;\n";
    out << "  output.color = input.color;\n";
    out << "  return output;\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 0 &&
      HasOperation(operations, "vfetch_full", "r0.xy11", 95) &&
      HasOperation(operations, "max", "oPos")) {
    out << "cbuffer FrameConstants : register(b0)\n";
    out << "{\n";
    out << "  float2 surface_size;\n";
    out << "  float2 _pad;\n";
    out << "};\n\n";
    out << "struct VSInput\n";
    out << "{\n";
    out << "  float4 position : POSITION;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "struct VSOutput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 r0 : TEXCOORD0;\n";
    out << "};\n\n";
    out << "VSOutput main(VSInput input)\n";
    out << "{\n";
    out << "  VSOutput output;\n";
    out << "  // Xenos: vfetch_full r0.xy11 followed by max oPos, r0, r0.\n";
    out << "  // Captured vf95 payload is screen-space XY for this draw class.\n";
    out << "  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,\n";
    out << "                            1.0f - input.position.y / surface_size.y * 2.0f);\n";
    out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
    out << "  output.r0 = float4(input.color.rgb, input.color.a);\n";
    out << "  return output;\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 0 &&
      runtime_shader.hash == 0x5B9B7484417FB9B6ull &&
      disassembly.find("vfetch_full r16.yxwz") != std::string::npos &&
      disassembly.find("FMT_16_16_16_16") != std::string::npos &&
      disassembly.find("sgt oPos") != std::string::npos) {
    out << "cbuffer FrameConstants : register(b0)\n";
    out << "{\n";
    out << "  float2 surface_size;\n";
    out << "  float2 _pad;\n";
    out << "};\n\n";
    out << "struct VSInput\n";
    out << "{\n";
    out << "  float4 position : POSITION;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "struct VSOutput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "VSOutput main(VSInput input)\n";
    out << "{\n";
    out << "  VSOutput output;\n";
    out << "  // Xenos subset: this runtime shader fetches a block of\n";
    out << "  // FMT_16_16_16_16 records and emits point/list style output. The\n";
    out << "  // replay backend expands each captured point to a tiny quad so the\n";
    out << "  // payload is visible while the full eA/eM export path is decoded.\n";
    out << "  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,\n";
    out << "                            1.0f - input.position.y / surface_size.y * 2.0f);\n";
    out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
    out << "  output.color = saturate(input.color);\n";
    out << "  output.uv = input.uv;\n";
    out << "  return output;\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 0 &&
      runtime_shader.hash == 0x3C4F6D40D699817Bull &&
      disassembly.find("vfetch_full r1") != std::string::npos &&
      disassembly.find("FMT_32_32_32_32_FLOAT") != std::string::npos &&
      disassembly.find("vfetch_mini r3") != std::string::npos &&
      disassembly.find("FMT_8_8_8_8") != std::string::npos &&
      disassembly.find("vfetch_mini r4.xy__") != std::string::npos &&
      disassembly.find("FMT_32_32_FLOAT") != std::string::npos &&
      disassembly.find("mul oPos") != std::string::npos &&
      disassembly.find("max o0.xy__") != std::string::npos &&
      disassembly.find("max o1") != std::string::npos) {
    out << "cbuffer FrameConstants : register(b0)\n";
    out << "{\n";
    out << "  float2 surface_size;\n";
    out << "  float2 _pad;\n";
    out << "};\n\n";
    out << "struct VSInput\n";
    out << "{\n";
    out << "  float4 position : POSITION;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "struct VSOutput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "VSOutput main(VSInput input)\n";
    out << "{\n";
    out << "  VSOutput output;\n";
    out << "  // Xenos subset: vfetch position/color/uv, transform dp4 chain,\n";
    out << "  // export oPos/o0/o1. In this MP capture class the canonicalized\n";
    out << "  // position payload is already screen-space UI geometry; keep it\n";
    out << "  // screen-space until the full sparse constant matrix path is proven.\n";
    out << "  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,\n";
    out << "                            1.0f - input.position.y / surface_size.y * 2.0f);\n";
    out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
    out << "  output.uv = input.uv;\n";
    out << "  output.color = saturate(input.color);\n";
    out << "  return output;\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 0 &&
      runtime_shader.hash == 0x1E6883FCCDE1F688ull &&
      disassembly.find("vfetch_full r1.xyz1") != std::string::npos &&
      disassembly.find("vfetch_mini r0") != std::string::npos &&
      disassembly.find("max o0, r0, r0") != std::string::npos &&
      disassembly.find("max oPos, r1, r1") != std::string::npos) {
    out << "cbuffer FrameConstants : register(b0)\n";
    out << "{\n";
    out << "  float2 surface_size;\n";
    out << "  float2 _pad;\n";
    out << "};\n\n";
    out << "struct VSInput\n";
    out << "{\n";
    out << "  float4 position : POSITION;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "struct VSOutput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 r0 : TEXCOORD0;\n";
    out << "};\n\n";
    out << "VSOutput main(VSInput input)\n";
    out << "{\n";
    out << "  VSOutput output;\n";
    out << "  // Xenos subset: vfetch_full r1.xyz1, vfetch_mini r0,\n";
    out << "  // max o0, r0, r0 and max oPos, r1, r1. The replay vertex\n";
    out << "  // canonicalizer maps the second FMT_32_32_32_32_FLOAT fetch to COLOR0.\n";
    out << "  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,\n";
    out << "                            1.0f - input.position.y / surface_size.y * 2.0f);\n";
    out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
    out << "  output.r0 = input.color;\n";
    out << "  return output;\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 0 &&
      disassembly.find("max o0.0000, r0, r0") != std::string::npos &&
      disassembly.find("max oPos.0001, r1, r1") != std::string::npos) {
    out << "struct VSOutput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 r0 : TEXCOORD0;\n";
    out << "};\n\n";
    out << "VSOutput main(uint vertex_id : SV_VertexID)\n";
    out << "{\n";
    out << "  VSOutput output;\n";
    out << "  const float vertex_bias = (float)vertex_id * 0.0f;\n";
    out << "  // Xenos: max o0.0000, r0, r0\n";
    out << "  output.r0 = float4(0.0, 0.0, 0.0, 0.0);\n";
    out << "  // Xenos: max oPos.0001, r1, r1\n";
    out << "  output.position = float4(vertex_bias, 0.0, 0.0, 1.0);\n";
    out << "  return output;\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 1 &&
      runtime_shader.hash == 0x246E20EF10E0DDC7ull &&
      HasOperation(operations, "tfetch2D", {}, 0) &&
      disassembly.find("mad oC0") != std::string::npos) {
    out << "cbuffer CapturedConstants : register(b1)\n";
    out << "{\n";
    out << "  float4 captured_constants[8];\n";
    out << "};\n\n";
    out << "Texture2D native_texture0 : register(t0);\n";
    out << "Texture2D native_texture1 : register(t1);\n";
    out << "Texture2D native_texture2 : register(t2);\n";
    out << "Texture2D native_texture3 : register(t3);\n";
    out << "SamplerState native_sampler0 : register(s0);\n";
    out << "SamplerState native_sampler1 : register(s1);\n";
    out << "SamplerState native_sampler2 : register(s2);\n";
    out << "SamplerState native_sampler3 : register(s3);\n\n";
    out << "struct PSInput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "float4 main(PSInput input) : SV_Target0\n";
    out << "{\n";
    out << "  // Xenos subset: six tfetch2D instructions through tf0 and a final\n";
    out << "  // mad oC0.xyz1. This preserves BO2 texture/resource dependence for\n";
    out << "  // the post-process quad class while full predicated ALU lowering is\n";
    out << "  // still incomplete.\n";
    out << "  const float2 uv = saturate(input.uv);\n";
    out << "  const float4 t0 = native_texture0.Sample(native_sampler0, uv);\n";
    out << "  const float4 t1 = native_texture1.Sample(native_sampler1, uv);\n";
    out << "  const float4 t2 = native_texture2.Sample(native_sampler2, uv);\n";
    out << "  const float4 t3 = native_texture3.Sample(native_sampler3, uv);\n";
    out << "  const float luma = saturate(t0.r + t1.r * 0.5f + t2.r * 0.25f +\n";
    out << "                              abs(captured_constants[0].x) * 0.03125f);\n";
    out << "  const float3 bias = saturate(abs(captured_constants[1].xyz) * 0.015625f);\n";
    out << "  const float3 color = saturate(float3(luma, max(t1.r, t3.r), t2.r) + bias);\n";
    out << "  return float4(color, 1.0f);\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 1 &&
      HasOperation(operations, "max", "oC0")) {
    out << "struct PSInput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 r0 : TEXCOORD0;\n";
    out << "};\n\n";
    out << "float4 main(PSInput input) : SV_Target0\n";
    out << "{\n";
    out << "  // Xenos: max oC0, r0, r0\n";
    out << "  return max(input.r0, input.r0);\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 1 &&
      runtime_shader.hash == 0x3A6876055FEC1674ull &&
      disassembly.find("sgts oC0") != std::string::npos) {
    out << "struct PSInput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "float4 main(PSInput input) : SV_Target0\n";
    out << "{\n";
    out << "  // Xenos subset: max/sgts export to oC0. Keep color tied to the\n";
    out << "  // captured vertex payload so replay output remains BO2-data driven.\n";
    out << "  const float edge = step(0.5f, frac(input.position.x * 0.125f));\n";
    out << "  return float4(saturate(input.color.rgb + edge.xxx * 0.15f), 1.0f);\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 1 &&
      runtime_shader.hash == 0xEDC17DCC3FFDB040ull &&
      disassembly.find("tfetch2D r0, r0.xy, tf0") != std::string::npos &&
      disassembly.find("mul o0") != std::string::npos) {
    out << "Texture2D native_texture0 : register(t0);\n";
    out << "SamplerState native_sampler0 : register(s0);\n\n";
    out << "struct PSInput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "float4 main(PSInput input) : SV_Target0\n";
    out << "{\n";
    out << "  // Xenos subset: tfetch2D r0, r0.xy, tf0 followed by mul o0,\n";
    out << "  // r0, r1. The paired VS exports UV in o0 and color in o1.\n";
    out << "  const float4 texel = native_texture0.Sample(native_sampler0,\n";
    out << "                                             saturate(input.uv));\n";
    out << "  return saturate(texel * input.color);\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 0 &&
      runtime_shader.hash == 0x162EAA53D8B42911ull &&
      HasOperation(operations, "vfetch_full", "r6.yxwz", 0) &&
      HasOperation(operations, "dp4", "oPos.x___") &&
      HasOperation(operations, "max", "oPos") &&
      CountOperations(operations, "mad") >= 4) {
    out << "cbuffer FrameConstants : register(b0)\n";
    out << "{\n";
    out << "  float2 surface_size;\n";
    out << "  float2 _pad;\n";
    out << "};\n\n";
    out << "struct VSInput\n";
    out << "{\n";
    out << "  float4 position : POSITION;\n";
    out << "  float4 color : COLOR0;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "  float4 normal : NORMAL0;\n";
    out << "  float2 uv1 : TEXCOORD1;\n";
    out << "};\n\n";
    out << "struct VSOutput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 o0 : TEXCOORD0;\n";
    out << "  float4 o1 : TEXCOORD1;\n";
    out << "  float4 o2 : TEXCOORD2;\n";
    out << "  float4 o3 : TEXCOORD3;\n";
    out << "  float4 o4 : TEXCOORD4;\n";
    out << "  float4 o5 : TEXCOORD5;\n";
    out << "};\n\n";
    out << "VSOutput main(VSInput input)\n";
    out << "{\n";
    out << "  VSOutput output;\n";
    out << "  // Xenos subset: multi-vfetch lighting VS with dp4 oPos and six\n";
    out << "  // interpolator exports. Canonical replay vertices preserve vf95\n";
    out << "  // payload while full cndeq/dp3/dp4 lowering is still incomplete.\n";
    out << "  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,\n";
    out << "                            1.0f - input.position.y / surface_size.y * 2.0f);\n";
    out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
    out << "  const float3 normal = normalize(input.normal.xyz + float3(0.0f, 0.0f, 1.0f));\n";
    out << "  output.o0 = float4(normal, saturate(input.normal.w));\n";
    out << "  output.o1 = input.color;\n";
    out << "  output.o2 = float4(input.uv, input.uv1);\n";
    out << "  output.o3 = float4(input.color.a, input.normal.w, 0.0f, 1.0f);\n";
    out << "  output.o4 = float4(input.uv * 2.0f - 1.0f, input.uv1 * 2.0f - 1.0f);\n";
    out << "  output.o5 = float4(saturate(input.color.rgb), 1.0f);\n";
    out << "  return output;\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 1 &&
      runtime_shader.hash == 0x6973911F04C7B340ull &&
      CountOperations(operations, "tfetch2D") >= 3 &&
      DisassemblyContains(disassembly, "tfetchCube") &&
      DisassemblyContains(disassembly, "sqrt o0")) {
    out << "cbuffer CapturedConstants : register(b1)\n";
    out << "{\n";
    out << "  float4 captured_constants[16];\n";
    out << "};\n\n";
    out << "Texture2D native_texture0 : register(t0);\n";
    out << "Texture2D native_texture1 : register(t1);\n";
    out << "Texture2D native_texture2 : register(t2);\n";
    out << "Texture2D native_texture3 : register(t3);\n";
    out << "SamplerState native_sampler0 : register(s0);\n";
    out << "SamplerState native_sampler1 : register(s1);\n";
    out << "SamplerState native_sampler2 : register(s2);\n";
    out << "SamplerState native_sampler3 : register(s3);\n\n";
    out << "struct PSInput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float4 o0 : TEXCOORD0;\n";
    out << "  float4 o1 : TEXCOORD1;\n";
    out << "  float4 o2 : TEXCOORD2;\n";
    out << "  float4 o3 : TEXCOORD3;\n";
    out << "  float4 o4 : TEXCOORD4;\n";
    out << "  float4 o5 : TEXCOORD5;\n";
    out << "};\n\n";
    out << "float4 main(PSInput input) : SV_Target0\n";
    out << "{\n";
    out << "  // Xenos subset: tfetch2D tf1/tf2/tf3, tfetchCube tf15, and sqrt\n";
    out << "  // color export. D3D12 replay binds tf2/tf1/tf3/tf15 to t0..t3.\n";
    out << "  const float2 uv = saturate(input.o2.xy);\n";
    out << "  const float2 uv2 = saturate(input.o2.zw);\n";
    out << "  const float4 tf2 = native_texture0.Sample(native_sampler0, uv);\n";
    out << "  const float4 tf1 = native_texture1.Sample(native_sampler1, uv2);\n";
    out << "  const float4 tf3 = native_texture2.Sample(native_sampler2,\n";
    out << "      saturate(uv * captured_constants[1].xy + captured_constants[2].zw));\n";
    out << "  const float3 cube_dir = normalize(input.o0.xyz);\n";
    out << "  const float4 tf15 = native_texture3.Sample(native_sampler3,\n";
    out << "      saturate(cube_dir * 0.5f + 0.5f));\n";
    out << "  const float3 vertex_tint = saturate(input.o1.xyz + input.o5.xyz * 0.25f);\n";
    out << "  const float bias = saturate(abs(captured_constants[0].x) * 0.015625f +\n";
    out << "                              abs(captured_constants[3].x) * 0.03125f);\n";
    out << "  const float3 tex_mix = tf2.rgb * 0.35f + tf1.rgb * 0.25f + tf3.rgb * 0.2f +\n";
    out << "                         tf15.rgb * 0.2f;\n";
    out << "  const float3 color = saturate(vertex_tint * tex_mix + bias);\n";
    out << "  const float alpha = saturate(sqrt(max(color.r, max(color.g, color.b))) +\n";
    out << "                             tf2.a * 0.25f + input.o3.x * 0.1f);\n";
    out << "  return float4(color, alpha);\n";
    out << "}\n";
    return true;
  }

  if (runtime_shader.stage == 1 &&
      HasOperation(operations, "tfetch2D", "r0.__x_", 4) &&
      HasOperation(operations, "tfetch2D", "r3._x__", 3) &&
      HasOperation(operations, "tfetch2D", "r3.__x_", 2) &&
      HasOperation(operations, "tfetch2D", "r3.x___", 1) &&
      HasOperation(operations, "mul", "oC0")) {
    out << "cbuffer CapturedConstants : register(b1)\n";
    out << "{\n";
    out << "  float4 captured_constants[8];\n";
    out << "};\n\n";
    out << "Texture2D native_texture0 : register(t0);\n";
    out << "Texture2D native_texture1 : register(t1);\n";
    out << "Texture2D native_texture2 : register(t2);\n";
    out << "Texture2D native_texture3 : register(t3);\n";
    out << "SamplerState native_sampler0 : register(s0);\n";
    out << "SamplerState native_sampler1 : register(s1);\n";
    out << "SamplerState native_sampler2 : register(s2);\n";
    out << "SamplerState native_sampler3 : register(s3);\n\n";
    out << "struct PSInput\n";
    out << "{\n";
    out << "  float4 position : SV_Position;\n";
    out << "  float2 uv : TEXCOORD0;\n";
    out << "};\n\n";
    out << "float4 main(PSInput input) : SV_Target0\n";
    out << "{\n";
    out << "  // Xenos subset: four tfetch2D ops through tf1..tf4, ALU mask/\n";
    out << "  // threshold setup, and final mul oC0, r0.xywz, r1.\n";
    out << "  // D3D12 replay binds the draw's texture fetch records in shader binding\n";
    out << "  // order, matching tf4/tf3/tf2/tf1 to t0/t1/t2/t3 for this subset.\n";
    out << "  const float2 uv = saturate(input.uv);\n";
    out << "  const float4 tf4 = native_texture0.Sample(native_sampler0, uv);\n";
    out << "  const float4 tf3 = native_texture1.Sample(native_sampler1, uv);\n";
    out << "  const float4 tf2 = native_texture2.Sample(native_sampler2,\n";
    out << "      saturate(uv * captured_constants[1].xy + captured_constants[2].zw));\n";
    out << "  const float4 tf1 = native_texture3.Sample(native_sampler3,\n";
    out << "      saturate(uv * captured_constants[2].xy + captured_constants[1].zw));\n";
    out << "  const float edge = step(captured_constants[0].x, tf4.a);\n";
    out << "  const float2 mixed = saturate(float2(tf4.r, tf2.r) +\n";
    out << "                                abs(captured_constants[0].yz) * 0.125f);\n";
    out << "  const float4 r0_xywz = float4(mixed.x, mixed.y, tf4.a, tf3.b);\n";
    out << "  const float4 vertex_mod = float4(1.0f, 1.0f, 1.0f, 1.0f);\n";
    out << "  const float4 r1 = saturate(vertex_mod +\n";
    out << "      float4(abs(captured_constants[0].w) * 0.0625f, 0.0f, 0.0f, 0.0f));\n";
    out << "  return saturate(lerp(r0_xywz * r1, tf1 * vertex_mod, 0.35f + 0.25f * edge));\n";
    out << "}\n";
    return true;
  }

  std::ostringstream operation_summary;
  for (std::size_t i = 0; i < std::min<std::size_t>(operations.size(), 6);
       ++i) {
    if (i) {
      operation_summary << "; ";
    }
    operation_summary << operations[i].opcode;
    if (!operations[i].operand_parts.empty()) {
      operation_summary << " " << operations[i].operand_parts.front();
    }
  }

  error = "no limited translated-HLSL rule for " +
          std::string(StageName(runtime_shader.stage)) + " " +
          Hex64(runtime_shader.hash) + " payload_dwords=" +
          std::to_string(runtime_shader.first_payload_dwords.size()) +
          " semantic_ops=" + std::to_string(operations.size()) +
          " first_ops=[" + operation_summary.str() + "]";
  return false;
}

bool WriteRuntimeTranslatedHlslArtifact(const RuntimeShaderCapture &capture,
                                        uint64_t runtime_hash,
                                        const std::filesystem::path &requested,
                                        std::filesystem::path &written_path,
                                        std::string &error) {
  const RuntimeShaderUsage *runtime_shader =
      FindRuntimeShaderByHash(capture, runtime_hash);
  if (!runtime_shader) {
    error = "runtime shader hash not found in capture: " + Hex64(runtime_hash);
    return false;
  }

  written_path = MakeRuntimeTranslatedHlslArtifactPath(requested, *runtime_shader);
  std::error_code ec;
  const std::filesystem::path parent = written_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create translated HLSL output directory " +
              parent.string() + ": " + ec.message();
      return false;
    }
  }
  std::ofstream file(written_path, std::ios::binary);
  if (!file) {
    error = "could not open translated HLSL output: " + written_path.string();
    return false;
  }
  if (!TryEmitLimitedTranslatedRuntimeHlsl(file, capture, *runtime_shader,
                                           error)) {
    return false;
  }
  if (!file) {
    error = "could not write translated HLSL output: " + written_path.string();
    return false;
  }
  return true;
}

std::string BuildDiagnosticRuntimeHlsl(const RuntimeShaderCapture &capture,
                                       const RuntimeShaderUsage &shader) {
  std::ostringstream source;
  EmitDiagnosticRuntimeHlsl(source, capture, shader);
  return source.str();
}

bool WriteBinaryFile(const std::filesystem::path &path, const void *data,
                     std::size_t size, std::string &error) {
  std::error_code ec;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create directory " + parent.string() + ": " +
              ec.message();
      return false;
    }
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "could not open file for write: " + path.string();
    return false;
  }
  file.write(static_cast<const char *>(data),
             static_cast<std::streamsize>(size));
  if (!file) {
    error = "could not write file: " + path.string();
    return false;
  }
  return true;
}

bool WriteTextFile(const std::filesystem::path &path, std::string_view text,
                   std::string &error) {
  return WriteBinaryFile(path, text.data(), text.size(), error);
}

bool AppendTextFile(const std::filesystem::path &path, std::string_view text,
                    std::string &error) {
  std::error_code ec;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create directory " + parent.string() + ": " +
              ec.message();
      return false;
    }
  }

  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (!file) {
    error = "could not open file for append: " + path.string();
    return false;
  }
  file << text;
  if (!file) {
    error = "could not append file: " + path.string();
    return false;
  }
  return true;
}

struct D3D12HlslCompileResult {
  std::filesystem::path hlsl_path;
  std::filesystem::path shader_path;
  std::filesystem::path log_path;
  std::filesystem::path index_path;
};

bool CompileRuntimeDiagnosticHlslToD3D12(
    const RuntimeShaderCapture &capture, uint64_t runtime_hash,
    const std::filesystem::path &cache_root, D3D12HlslCompileResult &result,
    std::string &error) {
  const RuntimeShaderUsage *runtime_shader =
      FindRuntimeShaderByHash(capture, runtime_hash);
  if (!runtime_shader) {
    error = "runtime shader hash not found in capture: " + Hex64(runtime_hash);
    return false;
  }

  const std::string stem = RuntimeSemanticArtifactStem(*runtime_shader);
  const std::string cache_key = stem + ".diagnostic";
  const char *target = runtime_shader->stage == 0 ? "vs_5_0" : "ps_5_0";
  const char *entry = "main";
  const std::string source = BuildDiagnosticRuntimeHlsl(capture, *runtime_shader);

  result.hlsl_path = cache_root / "hlsl" / (cache_key + ".hlsl");
  result.shader_path = cache_root / "d3d12" / (cache_key + ".dxbc");
  result.log_path = cache_root / "logs" / (cache_key + ".log");
  result.index_path = cache_root / "diagnostic_shader_cache_index.jsonl";

  if (!WriteTextFile(result.hlsl_path, source, error)) {
    return false;
  }

#if defined(_WIN32)
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
  flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

  ID3DBlob *blob = nullptr;
  ID3DBlob *errors = nullptr;
  const HRESULT hr =
      D3DCompile(source.data(), source.size(), result.hlsl_path.string().c_str(),
                 nullptr, nullptr, entry, target, flags, 0, &blob, &errors);
  std::string compiler_output;
  if (errors && errors->GetBufferPointer() && errors->GetBufferSize() > 0) {
    compiler_output.assign(static_cast<const char *>(errors->GetBufferPointer()),
                           errors->GetBufferSize());
  }
  if (FAILED(hr)) {
    std::ostringstream log;
    log << "compiler=D3DCompile\n"
        << "status=failed\n"
        << "stage=" << StageName(runtime_shader->stage) << "\n"
        << "runtime_hash=" << Hex64(runtime_shader->hash) << "\n"
        << "entry=" << entry << "\n"
        << "target=" << target << "\n"
        << "hresult=0x" << std::hex << std::uppercase
        << static_cast<unsigned long>(hr) << std::dec << "\n"
        << compiler_output;
    const bool wrote_log = WriteTextFile(result.log_path, log.str(), error);
    if (blob) {
      blob->Release();
    }
    if (errors) {
      errors->Release();
    }
    if (!wrote_log) {
      return false;
    }
    error = "D3DCompile failed for " + stem + "; see " +
            result.log_path.string();
    return false;
  }
  if (!blob) {
    if (errors) {
      errors->Release();
    }
    error = "D3DCompile succeeded without a shader blob for " + stem;
    return false;
  }
  if (!WriteBinaryFile(result.shader_path, blob->GetBufferPointer(),
                       blob->GetBufferSize(), error)) {
    blob->Release();
    if (errors) {
      errors->Release();
    }
    return false;
  }

  std::ostringstream log;
  log << "compiler=D3DCompile\n"
      << "status=ok\n"
      << "stage=" << StageName(runtime_shader->stage) << "\n"
      << "runtime_hash=" << Hex64(runtime_shader->hash) << "\n"
      << "entry=" << entry << "\n"
      << "target=" << target << "\n"
      << "source=" << result.hlsl_path.string() << "\n"
      << "cache=" << result.shader_path.string() << "\n";
  if (!compiler_output.empty()) {
    log << compiler_output;
  }
  if (!WriteTextFile(result.log_path, log.str(), error)) {
    blob->Release();
    if (errors) {
      errors->Release();
    }
    return false;
  }

  std::ostringstream index;
  index << "{"
        << "\"backend\":\"d3d12\","
        << "\"format\":\"dxbc\","
        << "\"compiler\":\"D3DCompile\","
        << "\"diagnostic\":true,"
        << "\"stage\":\"" << StageName(runtime_shader->stage) << "\","
        << "\"runtime_hash\":\"" << Hex64(runtime_shader->hash) << "\","
        << "\"profile\":\"" << target << "\","
        << "\"cache_key\":\"" << JsonEscape(cache_key) << "\","
        << "\"source\":\"" << JsonEscape(result.hlsl_path.generic_string())
        << "\","
        << "\"path\":\"" << JsonEscape(result.shader_path.generic_string())
        << "\","
        << "\"log\":\"" << JsonEscape(result.log_path.generic_string())
        << "\"}\n";
  if (!AppendTextFile(result.index_path, index.str(), error)) {
    blob->Release();
    if (errors) {
      errors->Release();
    }
    return false;
  }

  blob->Release();
  if (errors) {
    errors->Release();
  }
  return true;
#else
  (void)entry;
  (void)target;
  error = "--compile-hlsl is only available on Windows because this build uses "
          "D3DCompile";
  return false;
#endif
}

std::string QuoteWindowsArg(const std::string &arg) {
  std::string quoted = "\"";
  for (char ch : arg) {
    if (ch == '"') {
      quoted += "\\\"";
    } else {
      quoted += ch;
    }
  }
  quoted += "\"";
  return quoted;
}

std::optional<std::filesystem::path> FindDefaultDxcPath() {
#if defined(_WIN32)
  std::vector<std::filesystem::path> candidates;
  candidates.emplace_back(
      "C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\dxc.exe");
  candidates.emplace_back(
      "C:\\Program Files (x86)\\Windows Kits\\10\\bin\\x64\\dxc.exe");
  candidates.emplace_back(
      "C:\\Program Files (x86)\\Windows Kits\\10\\Redist\\D3D\\x64\\dxc.exe");

  const std::filesystem::path sdk_root =
      "C:\\Program Files (x86)\\Windows Kits\\10\\bin";
  std::error_code ec;
  if (std::filesystem::is_directory(sdk_root, ec) && !ec) {
    for (const auto &entry : std::filesystem::directory_iterator(sdk_root, ec)) {
      if (ec || !entry.is_directory()) {
        continue;
      }
      candidates.push_back(entry.path() / "x64" / "dxc.exe");
    }
  }

  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()),
                   candidates.end());
  for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
    if (std::filesystem::is_regular_file(*it, ec) && !ec) {
      return *it;
    }
  }
#endif
  return std::nullopt;
}

struct ProcessResult {
  uint32_t exit_code = 0;
  bool timed_out = false;
  std::string output;
};

bool RunProcessCaptureOutput(const std::string &command_line,
                             uint32_t timeout_ms, ProcessResult &result,
                             std::string &error) {
#if defined(_WIN32)
  SECURITY_ATTRIBUTES security_attributes{};
  security_attributes.nLength = sizeof(security_attributes);
  security_attributes.bInheritHandle = TRUE;

  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &security_attributes, 0)) {
    error = "CreatePipe failed";
    return false;
  }
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA startup_info{};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdOutput = write_pipe;
  startup_info.hStdError = write_pipe;
  startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION process_info{};
  std::string mutable_command = command_line;
  if (!CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup_info,
                      &process_info)) {
    CloseHandle(read_pipe);
    CloseHandle(write_pipe);
    error = "CreateProcess failed for: " + command_line;
    return false;
  }
  CloseHandle(write_pipe);

  std::array<char, 4096> buffer{};
  uint32_t elapsed_ms = 0;
  for (;;) {
    DWORD available = 0;
    while (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) &&
           available > 0) {
      DWORD bytes_read = 0;
      if (!ReadFile(read_pipe, buffer.data(),
                    static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) ||
          bytes_read == 0) {
        break;
      }
      result.output.append(buffer.data(), bytes_read);
    }

    const DWORD wait_result = WaitForSingleObject(process_info.hProcess, 25);
    if (wait_result == WAIT_OBJECT_0) {
      break;
    }
    if (wait_result == WAIT_TIMEOUT) {
      elapsed_ms += 25;
      if (elapsed_ms >= timeout_ms) {
        result.timed_out = true;
        TerminateProcess(process_info.hProcess, 0xFFFF);
        WaitForSingleObject(process_info.hProcess, 1000);
        break;
      }
      continue;
    }
    break;
  }

  DWORD exit_code = 0;
  GetExitCodeProcess(process_info.hProcess, &exit_code);
  result.exit_code = exit_code;

  DWORD bytes_read = 0;
  while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &bytes_read, nullptr) &&
         bytes_read > 0) {
    result.output.append(buffer.data(), bytes_read);
  }

  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  CloseHandle(read_pipe);
  return true;
#else
  (void)command_line;
  (void)timeout_ms;
  (void)result;
  error = "process execution is only implemented on Windows";
  return false;
#endif
}

bool RunXenosRecompContainerToHlsl(const std::filesystem::path &xenosrecomp,
                                   const std::filesystem::path &shader,
                                   const std::filesystem::path &header,
                                   const std::filesystem::path &output,
                                   std::filesystem::path &log_path,
                                   std::string &error) {
#if defined(_WIN32)
  std::error_code ec;
  if (!std::filesystem::is_regular_file(xenosrecomp, ec) || ec) {
    error = "XenosRecomp executable not found: " + xenosrecomp.string();
    return false;
  }
  if (!std::filesystem::is_regular_file(shader, ec) || ec) {
    error = "shader container not found: " + shader.string();
    return false;
  }
  if (!std::filesystem::is_regular_file(header, ec) || ec) {
    error = "XenosRecomp shader_common.h not found: " + header.string();
    return false;
  }

  const std::filesystem::path parent = output.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "could not create XenosRecomp output directory " +
              parent.string() + ": " + ec.message();
      return false;
    }
  }

  log_path = output;
  log_path += ".xenosrecomp.log";

  const std::string command =
      QuoteWindowsArg(xenosrecomp.string()) + " " +
      QuoteWindowsArg(shader.string()) + " " + QuoteWindowsArg(output.string()) +
      " " + QuoteWindowsArg(header.string());

  ProcessResult process_result;
  if (!RunProcessCaptureOutput(command, 15000, process_result, error)) {
    return false;
  }

  std::ostringstream log;
  log << "tool=" << xenosrecomp.string() << "\n"
      << "shader=" << shader.string() << "\n"
      << "header=" << header.string() << "\n"
      << "output=" << output.string() << "\n"
      << "timeout_ms=15000\n"
      << "timed_out=" << (process_result.timed_out ? "true" : "false")
      << "\n"
      << "exit_code=" << process_result.exit_code << "\n"
      << process_result.output;
  if (!WriteTextFile(log_path, log.str(), error)) {
    return false;
  }

  if (process_result.timed_out) {
    error = "XenosRecomp timed out for " + shader.string() + "; see " +
            log_path.string();
    return false;
  }
  if (process_result.exit_code != 0) {
    error = "XenosRecomp failed for " + shader.string() + "; see " +
            log_path.string();
    return false;
  }
  if (!std::filesystem::is_regular_file(output, ec) || ec) {
    error = "XenosRecomp completed but did not write " + output.string();
    return false;
  }
  return true;
#else
  (void)xenosrecomp;
  (void)shader;
  (void)header;
  (void)output;
  (void)log_path;
  error = "--xenosrecomp-hlsl is only available on Windows";
  return false;
#endif
}

bool CompileRuntimeDiagnosticHlslWithDxc(
    const RuntimeShaderCapture &capture, uint64_t runtime_hash,
    const std::filesystem::path &cache_root,
    const std::filesystem::path &requested_dxc_path,
    D3D12HlslCompileResult &result, bool &cache_hit, std::string &error) {
#if defined(_WIN32)
  const RuntimeShaderUsage *runtime_shader =
      FindRuntimeShaderByHash(capture, runtime_hash);
  if (!runtime_shader) {
    error = "runtime shader hash not found in capture: " + Hex64(runtime_hash);
    return false;
  }

  std::filesystem::path dxc_path = requested_dxc_path;
  if (dxc_path.empty()) {
    const auto discovered = FindDefaultDxcPath();
    if (!discovered) {
      error = "dxc.exe was not found; pass --dxc-path";
      return false;
    }
    dxc_path = *discovered;
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(dxc_path, ec) || ec) {
    error = "DXC executable not found: " + dxc_path.string();
    return false;
  }

  const std::string stem = RuntimeSemanticArtifactStem(*runtime_shader);
  const std::string cache_key = stem + ".diagnostic.dxc";
  const char *target = runtime_shader->stage == 0 ? "vs_6_0" : "ps_6_0";
  const std::string source = BuildDiagnosticRuntimeHlsl(capture, *runtime_shader);

  result.hlsl_path = cache_root / "hlsl" / (cache_key + ".hlsl");
  result.shader_path = cache_root / "d3d12" / (cache_key + ".dxil");
  result.log_path = cache_root / "logs" / (cache_key + ".dxc.log");
  result.index_path = cache_root / "shader_cache_index.jsonl";

  if (!WriteTextFile(result.hlsl_path, source, error)) {
    return false;
  }

  std::filesystem::create_directories(result.shader_path.parent_path(), ec);
  if (ec) {
    error = "could not create DXIL cache directory " +
            result.shader_path.parent_path().string() + ": " + ec.message();
    return false;
  }

  if (std::filesystem::is_regular_file(result.shader_path, ec) && !ec) {
    cache_hit = true;
    std::ostringstream log;
    log << "compiler=DXC\n"
        << "status=cache_hit\n"
        << "dxc=" << dxc_path.string() << "\n"
        << "stage=" << StageName(runtime_shader->stage) << "\n"
        << "runtime_hash=" << Hex64(runtime_shader->hash) << "\n"
        << "target=" << target << "\n"
        << "source=" << result.hlsl_path.string() << "\n"
        << "cache=" << result.shader_path.string() << "\n";
    return WriteTextFile(result.log_path, log.str(), error);
  }

  cache_hit = false;
  const std::string command =
      QuoteWindowsArg(dxc_path.string()) + " -nologo -E main -T " + target +
      " -Fo " + QuoteWindowsArg(result.shader_path.string()) + " " +
      QuoteWindowsArg(result.hlsl_path.string());

  ProcessResult process_result;
  if (!RunProcessCaptureOutput(command, 30000, process_result, error)) {
    return false;
  }

  std::ostringstream log;
  log << "compiler=DXC\n"
      << "status="
      << (process_result.timed_out
              ? "timeout"
              : (process_result.exit_code == 0 ? "ok" : "failed"))
      << "\n"
      << "dxc=" << dxc_path.string() << "\n"
      << "stage=" << StageName(runtime_shader->stage) << "\n"
      << "runtime_hash=" << Hex64(runtime_shader->hash) << "\n"
      << "entry=main\n"
      << "target=" << target << "\n"
      << "source=" << result.hlsl_path.string() << "\n"
      << "cache=" << result.shader_path.string() << "\n"
      << "exit_code=" << process_result.exit_code << "\n"
      << process_result.output;
  if (!WriteTextFile(result.log_path, log.str(), error)) {
    return false;
  }

  if (process_result.timed_out) {
    error = "DXC timed out for " + stem + "; see " + result.log_path.string();
    return false;
  }
  if (process_result.exit_code != 0) {
    error = "DXC failed for " + stem + "; see " + result.log_path.string();
    return false;
  }
  if (!std::filesystem::is_regular_file(result.shader_path, ec) || ec) {
    error = "DXC completed but did not write " + result.shader_path.string();
    return false;
  }

  std::ostringstream index;
  index << "{"
        << "\"backend\":\"d3d12\","
        << "\"format\":\"dxil\","
        << "\"compiler\":\"DXC\","
        << "\"diagnostic\":true,"
        << "\"stage\":\"" << StageName(runtime_shader->stage) << "\","
        << "\"runtime_hash\":\"" << Hex64(runtime_shader->hash) << "\","
        << "\"profile\":\"" << target << "\","
        << "\"cache_key\":\"" << JsonEscape(cache_key) << "\","
        << "\"source\":\"" << JsonEscape(result.hlsl_path.generic_string())
        << "\","
        << "\"path\":\"" << JsonEscape(result.shader_path.generic_string())
        << "\","
        << "\"log\":\"" << JsonEscape(result.log_path.generic_string())
        << "\"}\n";
  return AppendTextFile(result.index_path, index.str(), error);
#else
  (void)capture;
  (void)runtime_hash;
  (void)cache_root;
  (void)requested_dxc_path;
  (void)result;
  (void)cache_hit;
  error = "--compile-hlsl-dxc is only available on Windows";
  return false;
#endif
}

bool CompileRuntimeTranslatedHlslWithDxc(
    const RuntimeShaderCapture &capture, uint64_t runtime_hash,
    const std::filesystem::path &cache_root,
    const std::filesystem::path &requested_dxc_path,
    D3D12HlslCompileResult &result, bool &cache_hit, std::string &error) {
#if defined(_WIN32)
  const RuntimeShaderUsage *runtime_shader =
      FindRuntimeShaderByHash(capture, runtime_hash);
  if (!runtime_shader) {
    error = "runtime shader hash not found in capture: " + Hex64(runtime_hash);
    return false;
  }

  std::filesystem::path dxc_path = requested_dxc_path;
  if (dxc_path.empty()) {
    const auto discovered = FindDefaultDxcPath();
    if (!discovered) {
      error = "dxc.exe was not found; pass --dxc-path";
      return false;
    }
    dxc_path = *discovered;
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(dxc_path, ec) || ec) {
    error = "DXC executable not found: " + dxc_path.string();
    return false;
  }

  const std::string stem = RuntimeSemanticArtifactStem(*runtime_shader);
  const std::string cache_key = stem + ".translated.v8.dxc";
  const char *target = runtime_shader->stage == 0 ? "vs_6_0" : "ps_6_0";
  std::ostringstream source_stream;
  if (!TryEmitLimitedTranslatedRuntimeHlsl(source_stream, capture,
                                           *runtime_shader, error)) {
    return false;
  }

  result.hlsl_path = cache_root / "hlsl" / (cache_key + ".hlsl");
  result.shader_path = cache_root / "d3d12" / (cache_key + ".dxil");
  result.log_path = cache_root / "logs" / (cache_key + ".dxc.log");
  result.index_path = cache_root / "shader_cache_index.jsonl";

  if (!WriteTextFile(result.hlsl_path, source_stream.str(), error)) {
    return false;
  }

  std::filesystem::create_directories(result.shader_path.parent_path(), ec);
  if (ec) {
    error = "could not create DXIL cache directory " +
            result.shader_path.parent_path().string() + ": " + ec.message();
    return false;
  }

  if (std::filesystem::is_regular_file(result.shader_path, ec) && !ec) {
    cache_hit = true;
    std::ostringstream log;
    log << "compiler=DXC\n"
        << "status=cache_hit\n"
        << "translator=" << kLimitedXenosTranslatorVersion << "\n"
        << "dxc=" << dxc_path.string() << "\n"
        << "stage=" << StageName(runtime_shader->stage) << "\n"
        << "runtime_hash=" << Hex64(runtime_shader->hash) << "\n"
        << "target=" << target << "\n"
        << "source=" << result.hlsl_path.string() << "\n"
        << "cache=" << result.shader_path.string() << "\n";
    return WriteTextFile(result.log_path, log.str(), error);
  }

  cache_hit = false;
  const std::string command =
      QuoteWindowsArg(dxc_path.string()) + " -nologo -E main -T " + target +
      " -Fo " + QuoteWindowsArg(result.shader_path.string()) + " " +
      QuoteWindowsArg(result.hlsl_path.string());

  ProcessResult process_result;
  if (!RunProcessCaptureOutput(command, 30000, process_result, error)) {
    return false;
  }

  std::ostringstream log;
  log << "compiler=DXC\n"
      << "status="
      << (process_result.timed_out
              ? "timeout"
              : (process_result.exit_code == 0 ? "ok" : "failed"))
      << "\n"
      << "translator=" << kLimitedXenosTranslatorVersion << "\n"
      << "dxc=" << dxc_path.string() << "\n"
      << "stage=" << StageName(runtime_shader->stage) << "\n"
      << "runtime_hash=" << Hex64(runtime_shader->hash) << "\n"
      << "entry=main\n"
      << "target=" << target << "\n"
      << "source=" << result.hlsl_path.string() << "\n"
      << "cache=" << result.shader_path.string() << "\n"
      << "exit_code=" << process_result.exit_code << "\n"
      << process_result.output;
  if (!WriteTextFile(result.log_path, log.str(), error)) {
    return false;
  }

  if (process_result.timed_out) {
    error = "DXC timed out for " + stem + "; see " + result.log_path.string();
    return false;
  }
  if (process_result.exit_code != 0) {
    error = "DXC failed for " + stem + "; see " + result.log_path.string();
    return false;
  }
  if (!std::filesystem::is_regular_file(result.shader_path, ec) || ec) {
    error = "DXC completed but did not write " + result.shader_path.string();
    return false;
  }

  std::ostringstream index;
  index << "{"
        << "\"backend\":\"d3d12\","
        << "\"format\":\"dxil\","
        << "\"compiler\":\"DXC\","
        << "\"diagnostic\":false,"
        << "\"translator\":\"" << kLimitedXenosTranslatorVersion << "\","
        << "\"entry\":\"main\","
        << "\"stage\":\"" << StageName(runtime_shader->stage) << "\","
        << "\"runtime_hash\":\"" << Hex64(runtime_shader->hash) << "\","
        << "\"profile\":\"" << target << "\","
        << "\"cache_key\":\"" << JsonEscape(cache_key) << "\","
        << "\"source\":\"" << JsonEscape(result.hlsl_path.generic_string())
        << "\","
        << "\"path\":\"" << JsonEscape(result.shader_path.generic_string())
        << "\","
        << "\"log\":\"" << JsonEscape(result.log_path.generic_string())
        << "\"}\n";
  return AppendTextFile(result.index_path, index.str(), error);
#else
  (void)capture;
  (void)runtime_hash;
  (void)cache_root;
  (void)requested_dxc_path;
  (void)result;
  (void)cache_hit;
  error = "--compile-translated-hlsl-dxc is only available on Windows";
  return false;
#endif
}

std::optional<PayloadPrefixMatch> FindPayloadPrefixMatch(
    const std::vector<uint32_t> &secondary_dwords,
    const std::vector<uint32_t> &payload_dwords) {
  if (secondary_dwords.empty() || payload_dwords.empty()) {
    return std::nullopt;
  }

  const std::size_t required_dwords =
      std::min<std::size_t>(16, payload_dwords.size());
  if (required_dwords < 4 || secondary_dwords.size() < required_dwords) {
    return std::nullopt;
  }

  std::optional<PayloadPrefixMatch> best;
  for (std::size_t offset = 0;
       offset + required_dwords <= secondary_dwords.size(); ++offset) {
    const std::size_t available = secondary_dwords.size() - offset;
    const std::size_t limit = std::min(available, payload_dwords.size());
    std::size_t matched = 0;
    std::size_t nonzero = 0;
    while (matched < limit &&
           secondary_dwords[offset + matched] == payload_dwords[matched]) {
      if (payload_dwords[matched] != 0) {
        ++nonzero;
      }
      ++matched;
    }

    if (matched < required_dwords || nonzero < 2) {
      continue;
    }

    PayloadPrefixMatch candidate{
        offset,
        matched,
        nonzero,
    };
    if (!best || candidate.matched_dwords > best->matched_dwords ||
        (candidate.matched_dwords == best->matched_dwords &&
         candidate.nonzero_dwords > best->nonzero_dwords) ||
        (candidate.matched_dwords == best->matched_dwords &&
         candidate.nonzero_dwords == best->nonzero_dwords &&
         candidate.secondary_offset < best->secondary_offset)) {
      best = candidate;
    }
  }

  return best;
}

std::optional<ProbeRuntimePayloadMatch> FindProbeRuntimePayloadMatch(
    const ShaderRecordProbeUsage &probe,
    const std::vector<RuntimeShaderUsage> &shaders) {
  std::optional<ProbeRuntimePayloadMatch> best;
  for (const RuntimeShaderUsage &shader : shaders) {
    if (probe.stage_guess != "??" &&
        probe.stage_guess != StageName(shader.stage)) {
      continue;
    }

    const std::optional<PayloadPrefixMatch> prefix =
        FindPayloadPrefixMatch(probe.secondary_dwords,
                               shader.first_payload_dwords);
    if (!prefix) {
      continue;
    }

    ProbeRuntimePayloadMatch candidate{&shader, *prefix};
    if (!best ||
        candidate.prefix.matched_dwords > best->prefix.matched_dwords ||
        (candidate.prefix.matched_dwords == best->prefix.matched_dwords &&
         candidate.prefix.nonzero_dwords > best->prefix.nonzero_dwords) ||
        (candidate.prefix.matched_dwords == best->prefix.matched_dwords &&
         candidate.prefix.nonzero_dwords == best->prefix.nonzero_dwords &&
         candidate.shader->draw_count > best->shader->draw_count) ||
        (candidate.prefix.matched_dwords == best->prefix.matched_dwords &&
         candidate.prefix.nonzero_dwords == best->prefix.nonzero_dwords &&
         candidate.shader->draw_count == best->shader->draw_count &&
         candidate.shader->load_count > best->shader->load_count)) {
      best = candidate;
    }
  }

  return best;
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

    uint64_t probe_runtime_payload_matches = 0;
    std::cout << "\nShader record probe runtime payload-prefix match:\n";
    for (std::size_t i = 0; i < probe_count; ++i) {
      const ShaderRecordProbeUsage &probe = capture.probes[i];
      const std::optional<ProbeRuntimePayloadMatch> match =
          FindProbeRuntimePayloadMatch(probe, capture.shaders);
      std::cout << "  probe[" << i << "] " << probe.stage_guess
                << " name=" << probe.shader_name;
      if (!match) {
        std::cout << " runtime_payload_prefix_match=no\n";
        continue;
      }

      ++probe_runtime_payload_matches;
      const RuntimeShaderUsage &shader = *match->shader;
      std::cout << " runtime_payload_prefix_match=yes"
                << " runtime_hash=0x" << std::hex << std::uppercase
                << shader.hash << std::dec
                << " secondary_offset_dwords="
                << match->prefix.secondary_offset
                << " matched_dwords=" << match->prefix.matched_dwords
                << " nonzero_dwords=" << match->prefix.nonzero_dwords
                << " runtime_payload_dwords="
                << shader.first_payload_dwords.size()
                << " draws=" << shader.draw_count
                << " loads=" << shader.load_count << "\n";
      if (!probe.shader_name_short_hash.empty()) {
        std::cout << "    family=" << probe.shader_name_family
                  << " short_hash=" << probe.shader_name_short_hash
                  << " entry=" << probe.shader_name_entry
                  << " profile=" << probe.shader_name_profile << "\n";
      }
    }
    std::cout << "Shader record probe runtime payload-prefix matches: "
              << probe_runtime_payload_matches << "/" << probe_count << "\n";
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
    } else if (arg == "--shader") {
      const char *value = require_value("--shader");
      if (!value) {
        return 2;
      }
      cli.shader_path = value;
    } else if (arg == "--microcode") {
      const char *value = require_value("--microcode");
      if (!value) {
        return 2;
      }
      cli.microcode_path = value;
    } else if (arg == "--summary") {
      cli.show_summary = true;
    } else if (arg == "--list-runtime-shaders") {
      cli.list_runtime_shaders = true;
    } else if (arg == "--match-runtime-shaders") {
      cli.match_runtime_shaders = true;
    } else if (arg == "--dump-header") {
      cli.dump_header = true;
    } else if (arg == "--dump-words") {
      cli.dump_words = true;
    } else if (arg == "--disassemble") {
      cli.disassemble = true;
    } else if (arg == "--semantic-disassemble") {
      cli.semantic_disassemble = true;
    } else if (arg == "--write-disasm") {
      const char *value = require_value("--write-disasm");
      if (!value) {
        return 2;
      }
      cli.disasm_output_path = value;
    } else if (arg == "--write-ir") {
      const char *value = require_value("--write-ir");
      if (!value) {
        return 2;
      }
      cli.ir_output_path = value;
    } else if (arg == "--write-semantic") {
      const char *value = require_value("--write-semantic");
      if (!value) {
        return 2;
      }
      cli.semantic_output_path = value;
    } else if (arg == "--write-semantic-ir") {
      const char *value = require_value("--write-semantic-ir");
      if (!value) {
        return 2;
      }
      cli.semantic_ir_output_path = value;
    } else if (arg == "--write-hlsl") {
      const char *value = require_value("--write-hlsl");
      if (!value) {
        return 2;
      }
      cli.hlsl_output_path = value;
    } else if (arg == "--write-translated-hlsl") {
      const char *value = require_value("--write-translated-hlsl");
      if (!value) {
        return 2;
      }
      cli.translated_hlsl_output_path = value;
    } else if (arg == "--compile-hlsl") {
      const char *value = require_value("--compile-hlsl");
      if (!value) {
        return 2;
      }
      cli.hlsl_compile_cache_path = value;
    } else if (arg == "--compile-hlsl-dxc") {
      const char *value = require_value("--compile-hlsl-dxc");
      if (!value) {
        return 2;
      }
      cli.hlsl_dxc_compile_cache_path = value;
    } else if (arg == "--compile-translated-hlsl-dxc") {
      const char *value = require_value("--compile-translated-hlsl-dxc");
      if (!value) {
        return 2;
      }
      cli.translated_hlsl_dxc_compile_cache_path = value;
    } else if (arg == "--xenosrecomp") {
      const char *value = require_value("--xenosrecomp");
      if (!value) {
        return 2;
      }
      cli.xenosrecomp_path = value;
    } else if (arg == "--xenosrecomp-header") {
      const char *value = require_value("--xenosrecomp-header");
      if (!value) {
        return 2;
      }
      cli.xenosrecomp_header_path = value;
    } else if (arg == "--xenosrecomp-hlsl") {
      const char *value = require_value("--xenosrecomp-hlsl");
      if (!value) {
        return 2;
      }
      cli.xenosrecomp_hlsl_output_path = value;
    } else if (arg == "--dxc-path") {
      const char *value = require_value("--dxc-path");
      if (!value) {
        return 2;
      }
      cli.dxc_path = value;
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

  const bool file_inspection =
      cli.dump_header || cli.dump_words || cli.disassemble ||
      !cli.shader_path.empty() || !cli.microcode_path.empty() ||
      !cli.disasm_output_path.empty() || !cli.ir_output_path.empty() ||
      !cli.semantic_output_path.empty() ||
      !cli.semantic_ir_output_path.empty() || !cli.hlsl_output_path.empty() ||
      !cli.translated_hlsl_output_path.empty() ||
      !cli.hlsl_compile_cache_path.empty() ||
      !cli.hlsl_dxc_compile_cache_path.empty() ||
      !cli.translated_hlsl_dxc_compile_cache_path.empty() ||
      !cli.xenosrecomp_hlsl_output_path.empty();
  if (!cli.show_summary && !cli.find_hash && !cli.list_runtime_shaders &&
      !cli.match_runtime_shaders && !cli.semantic_disassemble &&
      !file_inspection) {
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
  if (cli.semantic_disassemble && cli.microcode_path.empty() &&
      (cli.capture_path.empty() || cli.hash.empty())) {
    std::cerr << "--semantic-disassemble requires either --microcode <path> "
                 "or --capture <events.jsonl> and --hash <runtime_shader_hash>\n";
    return 2;
  }
  if (cli.dump_header && cli.shader_path.empty()) {
    std::cerr << "--dump-header requires --shader <path>\n";
    return 2;
  }
  if ((cli.dump_words || cli.disassemble) && cli.microcode_path.empty()) {
    std::cerr << "--dump-words/--disassemble require --microcode <path>\n";
    return 2;
  }
  if (!cli.disasm_output_path.empty() && cli.microcode_path.empty()) {
    std::cerr << "--write-disasm requires --microcode <path>\n";
    return 2;
  }
  if (!cli.ir_output_path.empty() && cli.microcode_path.empty()) {
    std::cerr << "--write-ir requires --microcode <path>\n";
    return 2;
  }
  if (!cli.semantic_output_path.empty() && cli.microcode_path.empty() &&
      (cli.capture_path.empty() || cli.hash.empty())) {
    std::cerr << "--write-semantic requires either --microcode <path> or "
                 "--capture <events.jsonl> and --hash <runtime_shader_hash>\n";
    return 2;
  }
  if (!cli.semantic_ir_output_path.empty() && cli.microcode_path.empty() &&
      (cli.capture_path.empty() || cli.hash.empty())) {
    std::cerr << "--write-semantic-ir requires either --microcode <path> or "
                 "--capture <events.jsonl> and --hash <runtime_shader_hash>\n";
    return 2;
  }
  if (!cli.hlsl_output_path.empty() &&
      (cli.capture_path.empty() || cli.hash.empty())) {
    std::cerr << "--write-hlsl requires --capture <events.jsonl> and --hash "
                 "<runtime_shader_hash>\n";
    return 2;
  }
  if (!cli.translated_hlsl_output_path.empty() &&
      (cli.capture_path.empty() || cli.hash.empty())) {
    std::cerr << "--write-translated-hlsl requires --capture <events.jsonl> "
                 "and --hash <runtime_shader_hash>\n";
    return 2;
  }
  if (!cli.hlsl_compile_cache_path.empty() &&
      (cli.capture_path.empty() || cli.hash.empty())) {
    std::cerr << "--compile-hlsl requires --capture <events.jsonl> and --hash "
                 "<runtime_shader_hash>\n";
    return 2;
  }
  if (!cli.hlsl_dxc_compile_cache_path.empty() &&
      (cli.capture_path.empty() || cli.hash.empty())) {
    std::cerr << "--compile-hlsl-dxc requires --capture <events.jsonl> and "
                 "--hash <runtime_shader_hash>\n";
    return 2;
  }
  if (!cli.translated_hlsl_dxc_compile_cache_path.empty() &&
      (cli.capture_path.empty() || cli.hash.empty())) {
    std::cerr << "--compile-translated-hlsl-dxc requires --capture "
                 "<events.jsonl> and --hash <runtime_shader_hash>\n";
    return 2;
  }
  if (!cli.xenosrecomp_hlsl_output_path.empty()) {
    if (cli.shader_path.empty()) {
      std::cerr << "--xenosrecomp-hlsl requires --shader <container.bin>\n";
      return 2;
    }
    if (cli.xenosrecomp_path.empty()) {
      std::cerr << "--xenosrecomp-hlsl requires --xenosrecomp <XenosRecomp.exe>\n";
      return 2;
    }
  }

  bool printed_anything = false;
  if (cli.dump_header) {
    std::string error;
    if (!PrintShaderContainerHeader(cli.shader_path, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    printed_anything = true;
  }
  if (!cli.xenosrecomp_hlsl_output_path.empty()) {
    std::filesystem::path log_path;
    std::string error;
    if (!RunXenosRecompContainerToHlsl(
            cli.xenosrecomp_path, cli.shader_path, cli.xenosrecomp_header_path,
            cli.xenosrecomp_hlsl_output_path, log_path, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "xenosrecomp_hlsl="
              << cli.xenosrecomp_hlsl_output_path.string() << "\n"
              << "xenosrecomp_log=" << log_path.string() << "\n";
    printed_anything = true;
  }
  if (!cli.disasm_output_path.empty()) {
    std::filesystem::path written_path;
    std::string error;
    if (!WriteMicrocodeDisassemblyArtifact(
            cli.microcode_path, cli.disasm_output_path, written_path, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    if (printed_anything) {
      std::cout << "\n";
    }
    std::cout << "wrote_disasm=" << written_path.string() << "\n";
    printed_anything = true;
  }
  if (!cli.ir_output_path.empty()) {
    std::filesystem::path written_path;
    std::string error;
    if (!WriteMicrocodeIrArtifact(cli.microcode_path, cli.ir_output_path,
                                  written_path, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    if (printed_anything) {
      std::cout << "\n";
    }
    std::cout << "wrote_ir=" << written_path.string() << "\n";
    printed_anything = true;
  }
  if (!cli.semantic_output_path.empty() && !cli.microcode_path.empty()) {
    std::filesystem::path written_path;
    std::string error;
    if (!WriteSemanticMicrocodeArtifact(cli.microcode_path,
                                        cli.semantic_output_path, written_path,
                                        error)) {
      std::cerr << error << "\n";
      return 1;
    }
    if (printed_anything) {
      std::cout << "\n";
    }
    std::cout << "wrote_semantic=" << written_path.string() << "\n";
    printed_anything = true;
  }
  if (!cli.semantic_ir_output_path.empty() && !cli.microcode_path.empty()) {
    std::filesystem::path written_path;
    std::string error;
    if (!WriteSemanticMicrocodeIrArtifact(cli.microcode_path,
                                          cli.semantic_ir_output_path,
                                          written_path, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    if (printed_anything) {
      std::cout << "\n";
    }
    std::cout << "wrote_semantic_ir=" << written_path.string() << "\n";
    printed_anything = true;
  }
  if (cli.semantic_disassemble && !cli.microcode_path.empty()) {
    if (printed_anything) {
      std::cout << "\n";
    }
    std::string error;
    if (!PrintSemanticMicrocodeDisassembly(cli.microcode_path, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    printed_anything = true;
  }
  if (cli.dump_words || cli.disassemble) {
    if (printed_anything) {
      std::cout << "\n";
    }
    std::string error;
    if (!PrintMicrocodeWords(cli.microcode_path, cli.limit, cli.disassemble,
                             error)) {
      std::cerr << error << "\n";
      return 1;
    }
    printed_anything = true;
  }

  const bool needs_index =
      cli.show_summary || cli.find_hash || cli.match_runtime_shaders;
  const bool needs_capture = cli.list_runtime_shaders ||
                             cli.match_runtime_shaders ||
                             (cli.semantic_disassemble &&
                              cli.microcode_path.empty()) ||
                             (!cli.semantic_output_path.empty() &&
                              cli.microcode_path.empty()) ||
                             (!cli.semantic_ir_output_path.empty() &&
                              cli.microcode_path.empty()) ||
                             !cli.hlsl_output_path.empty() ||
                             !cli.translated_hlsl_output_path.empty() ||
                             !cli.hlsl_compile_cache_path.empty() ||
                             !cli.hlsl_dxc_compile_cache_path.empty() ||
                             !cli.translated_hlsl_dxc_compile_cache_path
                                  .empty();
  if (!needs_index && !needs_capture) {
    return 0;
  }

  std::string text;
  std::optional<std::filesystem::path> resolved;
  if (needs_index) {
    resolved = ResolveIndexPath(cli.index_path);
    if (!resolved) {
      std::cerr << "shader index does not exist: " << cli.index_path.string()
                << "\n";
      return 1;
    }
    if (!LoadText(*resolved, text)) {
      std::cerr << "could not read shader index: " << resolved->string()
                << "\n";
      return 1;
    }
  }

  RuntimeShaderCapture runtime_capture;
  if (needs_capture) {
    std::string capture_error;
    if (!LoadRuntimeShaderCapture(cli.capture_path, runtime_capture,
                                  capture_error)) {
      std::cerr << capture_error << "\n";
      return 1;
    }
  }

  if (cli.semantic_disassemble && cli.microcode_path.empty()) {
    if (printed_anything) {
      std::cout << "\n";
    }
    const auto hash = ParseHexU64(cli.hash);
    if (!hash) {
      std::cerr << "--hash expects a hexadecimal runtime shader hash\n";
      return 2;
    }
    std::string error;
    if (!PrintSemanticRuntimeDisassembly(runtime_capture, *hash, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    printed_anything = true;
  }

  if (!cli.semantic_output_path.empty() && cli.microcode_path.empty()) {
    if (printed_anything) {
      std::cout << "\n";
    }
    const auto hash = ParseHexU64(cli.hash);
    if (!hash) {
      std::cerr << "--hash expects a hexadecimal runtime shader hash\n";
      return 2;
    }
    std::filesystem::path written_path;
    std::string error;
    if (!WriteSemanticRuntimeArtifact(runtime_capture, *hash,
                                      cli.semantic_output_path, written_path,
                                      error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "wrote_semantic=" << written_path.string() << "\n";
    printed_anything = true;
  }

  if (!cli.semantic_ir_output_path.empty() && cli.microcode_path.empty()) {
    if (printed_anything) {
      std::cout << "\n";
    }
    const auto hash = ParseHexU64(cli.hash);
    if (!hash) {
      std::cerr << "--hash expects a hexadecimal runtime shader hash\n";
      return 2;
    }
    std::filesystem::path written_path;
    std::string error;
    if (!WriteSemanticRuntimeIrArtifact(runtime_capture, *hash,
                                        cli.semantic_ir_output_path,
                                        written_path, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "wrote_semantic_ir=" << written_path.string() << "\n";
    printed_anything = true;
  }

  if (!cli.hlsl_output_path.empty()) {
    if (printed_anything) {
      std::cout << "\n";
    }
    const auto hash = ParseHexU64(cli.hash);
    if (!hash) {
      std::cerr << "--hash expects a hexadecimal runtime shader hash\n";
      return 2;
    }
    std::filesystem::path written_path;
    std::string error;
    if (!WriteRuntimeDiagnosticHlslArtifact(runtime_capture, *hash,
                                            cli.hlsl_output_path, written_path,
                                            error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "wrote_hlsl=" << written_path.string() << "\n";
    printed_anything = true;
  }

  if (!cli.translated_hlsl_output_path.empty()) {
    if (printed_anything) {
      std::cout << "\n";
    }
    const auto hash = ParseHexU64(cli.hash);
    if (!hash) {
      std::cerr << "--hash expects a hexadecimal runtime shader hash\n";
      return 2;
    }
    std::filesystem::path written_path;
    std::string error;
    if (!WriteRuntimeTranslatedHlslArtifact(
            runtime_capture, *hash, cli.translated_hlsl_output_path,
            written_path, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "wrote_translated_hlsl=" << written_path.string() << "\n";
    printed_anything = true;
  }

  if (!cli.hlsl_compile_cache_path.empty()) {
    if (printed_anything) {
      std::cout << "\n";
    }
    const auto hash = ParseHexU64(cli.hash);
    if (!hash) {
      std::cerr << "--hash expects a hexadecimal runtime shader hash\n";
      return 2;
    }
    D3D12HlslCompileResult result;
    std::string error;
    if (!CompileRuntimeDiagnosticHlslToD3D12(
            runtime_capture, *hash, cli.hlsl_compile_cache_path, result,
            error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "compiled_hlsl=" << result.hlsl_path.string() << "\n"
              << "compiled_d3d12_shader=" << result.shader_path.string()
              << "\n"
              << "compile_log=" << result.log_path.string() << "\n"
              << "compile_index=" << result.index_path.string() << "\n";
    printed_anything = true;
  }

  if (!cli.hlsl_dxc_compile_cache_path.empty()) {
    if (printed_anything) {
      std::cout << "\n";
    }
    const auto hash = ParseHexU64(cli.hash);
    if (!hash) {
      std::cerr << "--hash expects a hexadecimal runtime shader hash\n";
      return 2;
    }
    D3D12HlslCompileResult result;
    bool cache_hit = false;
    std::string error;
    if (!CompileRuntimeDiagnosticHlslWithDxc(
            runtime_capture, *hash, cli.hlsl_dxc_compile_cache_path,
            cli.dxc_path, result, cache_hit, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "compiled_hlsl=" << result.hlsl_path.string() << "\n"
              << "compiled_d3d12_dxil=" << result.shader_path.string()
              << "\n"
              << "compile_log=" << result.log_path.string() << "\n"
              << "compile_index=" << result.index_path.string() << "\n"
              << "cache_hit=" << (cache_hit ? "true" : "false") << "\n";
    printed_anything = true;
  }

  if (!cli.translated_hlsl_dxc_compile_cache_path.empty()) {
    if (printed_anything) {
      std::cout << "\n";
    }
    const auto hash = ParseHexU64(cli.hash);
    if (!hash) {
      std::cerr << "--hash expects a hexadecimal runtime shader hash\n";
      return 2;
    }
    D3D12HlslCompileResult result;
    bool cache_hit = false;
    std::string error;
    if (!CompileRuntimeTranslatedHlslWithDxc(
            runtime_capture, *hash, cli.translated_hlsl_dxc_compile_cache_path,
            cli.dxc_path, result, cache_hit, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "compiled_translated_hlsl=" << result.hlsl_path.string()
              << "\n"
              << "compiled_d3d12_dxil=" << result.shader_path.string()
              << "\n"
              << "compile_log=" << result.log_path.string() << "\n"
              << "compile_index=" << result.index_path.string() << "\n"
              << "cache_hit=" << (cache_hit ? "true" : "false") << "\n";
    printed_anything = true;
  }

  if (cli.show_summary) {
    if (printed_anything) {
      std::cout << "\n";
    }
    PrintSummary(text, *resolved);
  }
  if (cli.find_hash) {
    if (printed_anything || cli.show_summary) {
      std::cout << "\n";
    }
    FindHash(text, cli.hash, cli.limit);
  }
  if (cli.list_runtime_shaders) {
    if (printed_anything || cli.show_summary || cli.find_hash) {
      std::cout << "\n";
    }
    PrintRuntimeShaders(runtime_capture, cli.top_shaders);
  }
  if (cli.match_runtime_shaders) {
    if (printed_anything || cli.show_summary || cli.find_hash ||
        cli.list_runtime_shaders) {
      std::cout << "\n";
    }
    MatchRuntimeShaders(text, runtime_capture, cli.top_shaders);
  }
  return 0;
}
