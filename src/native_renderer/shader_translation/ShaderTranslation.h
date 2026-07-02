#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "../replay/NativeRenderReplay.h"

namespace bo2::native {

enum class ShaderStage {
  Unknown,
  Vertex,
  Pixel,
};

struct ShaderBlobView {
  ShaderStage stage = ShaderStage::Unknown;
  const uint8_t* data = nullptr;
  uint32_t size = 0;
  std::string_view debug_name;
};

struct D3D12ShaderStageSource {
  std::filesystem::path path;
  std::filesystem::path cache_path;
  std::filesystem::path log_path;
  std::string cache_key;
  std::string source;
  std::string entry;
  std::string profile;
  bool manual_override = false;
  bool translated_cache = false;
};

struct D3D12ShaderProgramSource {
  D3D12ShaderStageSource vertex;
  D3D12ShaderStageSource pixel;
};

bool ResolveD3D12ShaderProgramSource(
    const replay::ReplayDrawState& draw_state,
    const replay::ReplayCliOptions& options,
    D3D12ShaderProgramSource& program,
    std::string& error);

}  // namespace bo2::native
