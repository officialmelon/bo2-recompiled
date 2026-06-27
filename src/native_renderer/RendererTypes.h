#pragma once

#include <cstdint>
#include <string>

namespace bo2::native {

enum class RendererMode {
  Emulated,
  Native,
  NativeNull,
};

enum class RendererBackendKind {
  None,
  NullDebug,
};

struct RendererConfig {
  RendererMode mode = RendererMode::Emulated;
  RendererBackendKind backend = RendererBackendKind::None;
  bool verbose = true;
  std::string app_name;
};

struct VdSwapInfo {
  uint32_t command_buffer = 0;
  uint32_t fetch_constant = 0;
  uint32_t writeback = 0;
  uint32_t system_command_buffer = 0;
  uint32_t system_command_buffer_token = 0;
  uint32_t frontbuffer_ptr = 0;
  uint32_t texture_format_ptr = 0;
  uint32_t color_space_ptr = 0;
  uint32_t width_ptr = 0;
  uint32_t height_ptr = 0;
};

const char* ToString(RendererMode mode);
const char* ToString(RendererBackendKind backend);
RendererMode ParseRendererMode(std::string mode);

}  // namespace bo2::native
