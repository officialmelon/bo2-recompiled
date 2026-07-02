#include "ShaderTranslation.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <system_error>

namespace bo2::native {
namespace {

bool ReadTextFile(const std::filesystem::path& path, std::string& text,
                  std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "could not open shader source " + path.string();
    return false;
  }
  file.seekg(0, std::ios::end);
  const std::ifstream::pos_type size = file.tellg();
  if (size == std::ifstream::pos_type(-1)) {
    error = "could not size shader source " + path.string();
    return false;
  }
  text.resize(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!text.empty()) {
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
      error = "could not read shader source " + path.string();
      return false;
    }
  }
  return true;
}

std::string ShaderHashFileKey(uint64_t hash) {
  const std::string hex = replay::FormatHex64(hash);
  return hex.size() > 2 && hex[0] == '0' && hex[1] == 'x' ? hex.substr(2)
                                                          : hex;
}

uint64_t Fnv1a64(std::string_view text) {
  uint64_t value = 14695981039346656037ull;
  for (const char ch : text) {
    value ^= static_cast<uint8_t>(ch);
    value *= 1099511628211ull;
  }
  return value;
}

std::string LowerAscii(std::string text) {
  for (char& ch : text) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return text;
}

bool StageMatches(std::string stage, const char* short_stage,
                  const char* long_stage) {
  stage = LowerAscii(std::move(stage));
  return stage == short_stage || stage == long_stage;
}

std::filesystem::path ResolveOverridePath(const std::filesystem::path& root,
                                          const std::filesystem::path& path) {
  return path.is_absolute() ? path : root / path;
}

std::filesystem::path ResolveCacheRecordPath(const std::filesystem::path& root,
                                             const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) {
    return path;
  }

  std::error_code ec;
  const std::filesystem::path cwd_relative = std::filesystem::absolute(path, ec);
  if (!ec && std::filesystem::exists(cwd_relative, ec) && !ec) {
    return cwd_relative;
  }

  std::filesystem::path project_root;
  if (root.filename() == "cache" &&
      root.parent_path().filename() == "shader_work") {
    project_root = root.parent_path().parent_path();
  }
  if (!project_root.empty()) {
    const std::filesystem::path project_relative = project_root / path;
    if (std::filesystem::exists(project_relative, ec) && !ec) {
      return project_relative;
    }
  }

  const std::filesystem::path root_relative = root / path;
  if (std::filesystem::exists(root_relative, ec) && !ec) {
    return root_relative;
  }
  return path;
}

bool FindManifestOverridePath(const std::filesystem::path& root,
                              const char* short_stage,
                              const char* long_stage, uint64_t hash,
                              std::filesystem::path& path, std::string& entry,
                              std::string& profile, std::string& error) {
  const std::filesystem::path manifest = root / "overrides.json";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(manifest, ec) || ec) {
    return false;
  }

  std::vector<replay::ShaderOverrideRecord> records;
  if (!replay::LoadShaderOverrideManifest(manifest, records, error)) {
    return false;
  }

  for (const replay::ShaderOverrideRecord& record : records) {
    if (LowerAscii(record.backend) != "d3d12" ||
        record.runtime_hash != hash ||
        !StageMatches(record.stage, short_stage, long_stage)) {
      continue;
    }
    path = ResolveOverridePath(root, record.path);
    if (!record.entry.empty()) {
      entry = record.entry;
    }
    if (!record.profile.empty()) {
      profile = record.profile;
    }
    return true;
  }
  return false;
}

std::filesystem::path FindShaderOverridePath(const std::filesystem::path& root,
                                             const char* short_stage,
                                             const char* long_stage,
                                             uint64_t hash) {
  const std::string key = ShaderHashFileKey(hash);
  const std::filesystem::path stage_root = root / "d3d12";
  const std::array<std::filesystem::path, 4> candidates = {
      stage_root / (std::string(short_stage) + "_" + key + ".hlsl"),
      stage_root / (std::string(long_stage) + "_" + key + ".hlsl"),
      stage_root / (key + "." + short_stage + ".hlsl"),
      stage_root / (key + ".hlsl"),
  };

  for (const std::filesystem::path& candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
      return candidate;
    }
  }
  return {};
}

bool FindCacheShaderPath(const std::filesystem::path& root,
                         const char* short_stage, const char* long_stage,
                         uint64_t hash, D3D12ShaderStageSource& stage,
                         std::string& error) {
  const std::array<std::filesystem::path, 2> indexes = {
      root / "shader_cache_index.json",
      root / "shader_cache_index.jsonl",
  };
  bool found_index = false;
  std::error_code ec;
  for (const std::filesystem::path& index : indexes) {
    if (!std::filesystem::is_regular_file(index, ec) || ec) {
      continue;
    }
    found_index = true;

    std::vector<replay::ShaderCacheRecord> records;
    if (!replay::LoadShaderCacheIndex(index, records, error)) {
      return false;
    }

    for (auto record_it = records.rbegin(); record_it != records.rend();
         ++record_it) {
      const replay::ShaderCacheRecord& record = *record_it;
      if (record.diagnostic || LowerAscii(record.backend) != "d3d12" ||
          record.runtime_hash != hash ||
          !StageMatches(record.stage, short_stage, long_stage)) {
        continue;
      }
      const std::string format = LowerAscii(record.format);
      if (!format.empty() && format != "dxbc" && format != "dxil") {
        continue;
      }
      stage.cache_path = ResolveCacheRecordPath(root, record.path);
      stage.path = ResolveCacheRecordPath(root, record.source);
      stage.entry = record.entry;
      stage.profile = record.profile;
      stage.cache_key = record.cache_key;
      stage.translated_cache = true;
      if (stage.path.filename().string().find(".translated.") !=
              std::string::npos ||
          stage.path.filename().string().find(".vertexless.") !=
              std::string::npos) {
        stage.entry = "main";
      }
      return true;
    }
  }

  const std::filesystem::path d3d12_root = root / "d3d12";
  if (std::filesystem::is_directory(d3d12_root, ec) && !ec) {
    const std::string stage_prefix =
        std::string(short_stage[0] == 'v' ? "VS_" : "PS_") +
        replay::FormatHex64(hash) + ".";
    std::filesystem::path best_path;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(d3d12_root, ec)) {
      if (ec || !entry.is_regular_file()) {
        continue;
      }
      const std::string filename = entry.path().filename().string();
      if (filename.rfind(stage_prefix, 0) != 0) {
        continue;
      }
      if (filename.ends_with(".d3dcompile.dxbc") ||
          filename.ends_with(".dxbc")) {
        if (best_path.empty() || filename > best_path.filename().string()) {
          best_path = entry.path();
        }
      }
    }
    if (!best_path.empty()) {
      stage.cache_path = best_path;
      std::string stem = best_path.filename().string();
      if (stem.ends_with(".d3dcompile.dxbc")) {
        stem.resize(stem.size() - std::strlen(".d3dcompile.dxbc"));
      } else if (stem.ends_with(".dxbc")) {
        stem.resize(stem.size() - std::strlen(".dxbc"));
      }
      stage.cache_key = stem;
      stage.path = root / "hlsl" / (stem + ".hlsl");
      if (!std::filesystem::is_regular_file(stage.path, ec) || ec) {
        stage.path.clear();
      }
      stage.profile = std::string(short_stage) + "_5_0";
      if (stem.find(".translated.") != std::string::npos ||
          stem.find(".vertexless.") != std::string::npos) {
        stage.entry = "main";
      }
      stage.translated_cache = true;
      return true;
    }
  }
  if (!found_index) {
    error.clear();
  }
  return false;
}

std::string MakeOverrideCacheKey(const char* short_stage, uint64_t hash,
                                 const std::string& profile,
                                 const std::string& source) {
  constexpr const char* kBindingLayoutVersion = "layout8";
  return std::string("manual_") + short_stage + "_" + ShaderHashFileKey(hash) +
         "_" + profile + "_" + kBindingLayoutVersion + "_src" +
         ShaderHashFileKey(Fnv1a64(source));
}

bool ResolveD3D12ShaderStage(const replay::ReplayCliOptions& options,
                             const char* short_stage,
                             const char* long_stage, uint64_t hash,
                             D3D12ShaderStageSource& stage,
                             std::string& error) {
  stage.entry = std::string(short_stage[0] == 'v' ? "VSMain" : "PSMain");
  stage.profile = std::string(short_stage) + "_5_0";

  std::string manifest_error;
  std::filesystem::path override_path;
  if (!FindManifestOverridePath(options.shader_override_root, short_stage,
                                long_stage, hash, override_path, stage.entry,
                                stage.profile, manifest_error)) {
    if (!manifest_error.empty()) {
      error = manifest_error;
      return false;
    }
    override_path =
        FindShaderOverridePath(options.shader_override_root, short_stage,
                               long_stage, hash);
  }

  if (!override_path.empty()) {
    if (!ReadTextFile(override_path, stage.source, error)) {
      return false;
    }
    stage.path = override_path;
    stage.cache_key =
        MakeOverrideCacheKey(short_stage, hash, stage.profile, stage.source);
    stage.cache_path =
        options.shader_cache_root / "d3d12" / (stage.cache_key + ".dxbc");
    stage.log_path =
        options.shader_cache_root / "logs" / (stage.cache_key + ".log");
    stage.manual_override = true;
    return true;
  }

  std::string cache_error;
  if (FindCacheShaderPath(options.shader_cache_root, short_stage, long_stage,
                          hash, stage, cache_error)) {
    if (!stage.cache_key.empty()) {
      stage.log_path =
          options.shader_cache_root / "logs" / (stage.cache_key + ".log");
    }
    if (stage.path.empty()) {
      stage.path = stage.cache_path;
    } else {
      std::error_code ec;
      if (std::filesystem::is_regular_file(stage.path, ec) && !ec &&
          !ReadTextFile(stage.path, stage.source, error)) {
        return false;
      }
      if (!stage.source.empty() && stage.cache_key.rfind("manual_", 0) == 0) {
        const std::string source_cache_key = MakeOverrideCacheKey(
            short_stage, hash, stage.profile, stage.source);
        if (source_cache_key != stage.cache_key) {
          stage.cache_key = source_cache_key;
          stage.cache_path = options.shader_cache_root / "d3d12" /
                             (stage.cache_key + ".dxbc");
          stage.log_path = options.shader_cache_root / "logs" /
                           (stage.cache_key + ".log");
        }
      }
      if (!stage.source.empty() && stage.cache_path.extension() == ".dxil") {
        stage.profile = std::string(short_stage) + "_5_0";
        stage.cache_path = options.shader_cache_root / "d3d12" /
                           (stage.cache_key + ".d3dcompile.dxbc");
        stage.log_path = options.shader_cache_root / "logs" /
                         (stage.cache_key + ".d3dcompile.log");
      }
    }
    return true;
  }
  if (!cache_error.empty()) {
    error = cache_error;
    return false;
  }

  error = "no translated, cached, or override shader is available for " +
          std::string(long_stage) + " shader " + replay::FormatHex64(hash);
  return false;
}

bool ApplyShaderVariant(D3D12ShaderStageSource& stage,
                        const std::filesystem::path& shader_cache_root,
                        const char* profile, const char* cache_key,
                        const char* source_name, std::string& error) {
  stage.cache_key = cache_key;
  stage.path = shader_cache_root / "hlsl" / source_name;
  stage.cache_path = shader_cache_root / "d3d12" /
                     (std::string(cache_key) + ".d3dcompile.dxbc");
  stage.log_path = shader_cache_root / "logs" /
                   (std::string(cache_key) + ".d3dcompile.log");
  stage.entry = "main";
  stage.profile = profile;
  stage.source.clear();
  stage.translated_cache = true;
  return ReadTextFile(stage.path, stage.source, error);
}

void ApplyInlinePixelVariant(D3D12ShaderStageSource& stage,
                             const std::filesystem::path& shader_cache_root,
                             const char* cache_key, const char* source_name,
                             const char* source) {
  stage.cache_key = cache_key;
  stage.path = shader_cache_root / "hlsl" / source_name;
  stage.cache_path = shader_cache_root / "d3d12" /
                     (std::string(cache_key) + ".d3dcompile.dxbc");
  stage.log_path = shader_cache_root / "logs" /
                   (std::string(cache_key) + ".d3dcompile.log");
  stage.entry = "main";
  stage.profile = "ps_5_0";
  stage.source = source ? source : "";
  stage.translated_cache = true;
}

}  // namespace

bool ResolveD3D12ShaderProgramSource(
    const replay::ReplayDrawState& draw_state,
    const replay::ReplayCliOptions& options,
    D3D12ShaderProgramSource& program,
    std::string& error) {
  program = {};

  std::string vertex_error;
  if (!ResolveD3D12ShaderStage(options, "vs", "vertex",
                               draw_state.vertex_shader.hash, program.vertex,
                               vertex_error)) {
    error = "no translated, cached, or override shader pair is available for "
            "draw " +
            std::to_string(draw_state.draw_index) + " (VS=" +
            replay::FormatHex64(draw_state.vertex_shader.hash) + ", PS=" +
            replay::FormatHex64(draw_state.pixel_shader.hash) +
            ", override_root=" + options.shader_override_root.string() +
            "): " + vertex_error +
            ". Re-run with --allow-diagnostic-shader only for the temporary "
            "resource-backed geometry diagnostic path.";
    return false;
  }

  std::string pixel_error;
  if (!ResolveD3D12ShaderStage(options, "ps", "pixel",
                               draw_state.pixel_shader.hash, program.pixel,
                               pixel_error)) {
    error = "no translated, cached, or override shader pair is available for "
            "draw " +
            std::to_string(draw_state.draw_index) + " (VS=" +
            replay::FormatHex64(draw_state.vertex_shader.hash) + ", PS=" +
            replay::FormatHex64(draw_state.pixel_shader.hash) +
            ", override_root=" + options.shader_override_root.string() +
            "): " + pixel_error +
            ". Re-run with --allow-diagnostic-shader only for the temporary "
            "resource-backed geometry diagnostic path.";
    return false;
  }

  const bool ab1e_vertex_shader =
      draw_state.vertex_shader.hash == 0xAB1E86137A0240E8ull;
  if (draw_state.pixel_shader.hash == 0xC4ED2979F29C9139ull) {
    if (ab1e_vertex_shader) {
      if (!ApplyShaderVariant(program.pixel, options.shader_cache_root,
                              "ps_5_0",
                              "PS_0xC4ED2979F29C9139.translated.v7.dxc",
                              "PS_0xC4ED2979F29C9139.translated.v7.dxc.hlsl",
                              pixel_error)) {
        error = "could not load pair-specific pixel shader variant for draw " +
                std::to_string(draw_state.draw_index) + " VS=" +
                replay::FormatHex64(draw_state.vertex_shader.hash) + " PS=" +
                replay::FormatHex64(draw_state.pixel_shader.hash) + ": " +
                pixel_error;
        return false;
      }
    } else if (!ApplyShaderVariant(
                   program.pixel, options.shader_cache_root, "ps_5_0",
                   "PS_0xC4ED2979F29C9139.translated.v5.dxc",
                   "PS_0xC4ED2979F29C9139.translated.v5.dxc.hlsl",
                   pixel_error)) {
      error = "could not load pair-specific pixel shader variant for draw " +
              std::to_string(draw_state.draw_index) + " VS=" +
              replay::FormatHex64(draw_state.vertex_shader.hash) + " PS=" +
              replay::FormatHex64(draw_state.pixel_shader.hash) + ": " +
              pixel_error;
      return false;
    }
  } else if (draw_state.pixel_shader.hash == 0x246E20EF10E0DDC7ull) {
    if (!ApplyShaderVariant(program.pixel, options.shader_cache_root, "ps_5_0",
                            "PS_0x246E20EF10E0DDC7.translated.v8.dxc",
                            "PS_0x246E20EF10E0DDC7.translated.v8.dxc.hlsl",
                            pixel_error)) {
      error = "could not load pair-specific pixel shader variant for draw " +
              std::to_string(draw_state.draw_index) + " VS=" +
              replay::FormatHex64(draw_state.vertex_shader.hash) + " PS=" +
              replay::FormatHex64(draw_state.pixel_shader.hash) + ": " +
              pixel_error;
      return false;
    }
  } else if (ab1e_vertex_shader &&
             draw_state.pixel_shader.hash == 0xA4A965C189287B99ull) {
    static constexpr const char* kAb1eA4ZeroPixelShader = R"(
// BO2 native renderer pair-specific zero-output PS.
// VS 0xAB1E86137A0240E8 exports position only; it does not declare an
// interpolator feeding A4's r0 input. Replaying this pair with the generic A4
// r0 passthrough produced an unproven diagonal fullscreen artifact.
struct PSInput
{
  float4 position : SV_Position;
};

float4 main(PSInput input) : SV_Target0
{
  return input.position.xxxx * 0.0f;
}
)";
    ApplyInlinePixelVariant(program.pixel, options.shader_cache_root,
                            "PS_0xA4A965C189287B99.ab1e_zero.v1.dxc",
                            "PS_0xA4A965C189287B99.ab1e_zero.v1.dxc.hlsl",
                            kAb1eA4ZeroPixelShader);
  }

  return true;
}

}  // namespace bo2::native
