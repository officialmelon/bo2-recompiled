#pragma once

#include <cstdint>
#include <string_view>

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

}  // namespace bo2::native
