#pragma once

#include <cstdint>

namespace bo2::native {

enum class RenderCommandType {
  BeginFrame,
  EndFrame,
  Present,
  BindShader,
  BindTexture,
  SetRenderTarget,
  DrawIndexed,
  Unsupported,
};

struct RenderCommand {
  RenderCommandType type = RenderCommandType::Unsupported;
  uint64_t sequence = 0;
  uint32_t guest_address = 0;
  uint32_t arg0 = 0;
  uint32_t arg1 = 0;
  uint32_t arg2 = 0;
  uint32_t arg3 = 0;
};

}  // namespace bo2::native
