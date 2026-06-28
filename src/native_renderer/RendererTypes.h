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
  static constexpr std::size_t kMaxDwords = 128;

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
  uint32_t command_buffer_object = 0;
  uint32_t write_begin = 0;
  uint32_t write_end = 0;
  uint32_t write_limit = 0;
  uint32_t dword_count = 0;
  std::array<uint32_t, kMaxDwords> dwords{};
  bool truncated = false;
  bool has_draw_indx_2 = false;
  uint32_t draw_packet_dword_offset = 0;
  uint32_t draw_packet = 0;
  uint32_t draw_initiator = 0;
  uint32_t index_count = 0;
  uint32_t primitive_type = 0;
  uint32_t source_select = 0;
  bool index_32bit = false;
};

struct CommandBufferEventInfo {
  uint64_t event_index = 0;
  uint32_t function_address = 0;
  std::string_view function_name;
  uint64_t link_register = 0;
  uint32_t r3 = 0;
  uint32_t r4 = 0;
  uint32_t r5 = 0;
  uint32_t r6 = 0;
  uint32_t command_buffer_object = 0;
  uint32_t write_begin = 0;
  uint32_t write_end = 0;
  uint32_t write_limit_begin = 0;
  uint32_t write_limit_end = 0;
  uint32_t return_value = 0;
};

struct PM4PacketInfo {
  static constexpr std::size_t kMaxDwords = 4;

  uint64_t event_index = 0;
  uint32_t opcode = 0;
  uint32_t packet = 0;
  uint32_t payload_dword_count = 0;
  uint32_t packet_ptr = 0;
  uint32_t buffer_ptr = 0;
  uint32_t packet_offset = 0;
  uint32_t first_dword_count = 0;
  std::array<uint32_t, kMaxDwords> first_dwords{};
};

struct PM4DrawInfo {
  uint64_t event_index = 0;
  std::string_view opcode_name;
  uint32_t opcode = 0;
  uint32_t packet = 0;
  uint32_t packet_ptr = 0;
  uint32_t buffer_ptr = 0;
  uint32_t packet_offset = 0;
  uint32_t index_count = 0;
  uint32_t primitive_type = 0;
  uint32_t source_select = 0;
  bool indexed = false;
  uint32_t index_base = 0;
  uint32_t index_length = 0;
  uint32_t index_buffer_count = 0;
  uint32_t index_format = 0;
  uint32_t index_endianness = 0;
  uint32_t major_mode = 0;
  bool explicit_major_mode = false;
  uint32_t viz_query_condition = 0;
  uint64_t vertex_shader_hash = 0;
  uint64_t pixel_shader_hash = 0;
};

struct PM4ShaderInfo {
  uint64_t event_index = 0;
  uint32_t opcode = 0;
  uint32_t packet = 0;
  uint32_t packet_ptr = 0;
  uint32_t buffer_ptr = 0;
  uint32_t packet_offset = 0;
  uint32_t shader_type = 0;
  bool embedded = false;
  uint32_t guest_address = 0;
  uintptr_t host_address = 0;
  uint32_t dword_count = 0;
  uint64_t shader_hash = 0;
};

struct PM4ConstantInfo {
  uint64_t event_index = 0;
  uint32_t opcode = 0;
  uint32_t packet = 0;
  uint32_t packet_ptr = 0;
  uint32_t buffer_ptr = 0;
  uint32_t packet_offset = 0;
  uint32_t address = 0;
  uint32_t offset_type = 0;
  uint32_t type = 0;
  uint32_t index = 0;
  uint32_t dword_count = 0;
};

struct PM4SwapInfo {
  uint64_t event_index = 0;
  uint32_t opcode = 0;
  uint32_t packet = 0;
  uint32_t packet_ptr = 0;
  uint32_t buffer_ptr = 0;
  uint32_t packet_offset = 0;
  uint32_t frontbuffer_ptr = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t frame_counter = 0;
};

const char* ToString(RendererMode mode);
const char* ToString(RendererBackendKind backend);
RendererMode ParseRendererMode(std::string mode);

}  // namespace bo2::native
