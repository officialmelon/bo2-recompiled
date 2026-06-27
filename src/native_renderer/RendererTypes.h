#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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
  uint64_t frame_index = 0;
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

struct CommandBufferSnapshot {
  static constexpr std::size_t kMaxDwords = 16;

  uint64_t frame_index = 0;
  uint32_t command_buffer = 0;
  uint32_t dword_count = 0;
  std::array<uint32_t, kMaxDwords> dwords{};
  bool has_xe_swap = false;
  uint32_t xe_swap_dword_offset = 0;
  uint32_t xe_swap_packet = 0;
  uint32_t swap_signature = 0;
  uint32_t frontbuffer_physical = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

struct DrawPacketCandidateInfo {
  uint64_t event_index = 0;
  uint32_t function_address = 0;
  std::string_view function_name;
  uint64_t link_register = 0;
  uint32_t r3 = 0;
  uint32_t r4 = 0;
  uint32_t r5 = 0;
  uint32_t r6 = 0;
  uint32_t r7 = 0;
  uint32_t r8 = 0;
  uint32_t r31 = 0;
};

const char* ToString(RendererMode mode);
const char* ToString(RendererBackendKind backend);
RendererMode ParseRendererMode(std::string mode);

}  // namespace bo2::native
