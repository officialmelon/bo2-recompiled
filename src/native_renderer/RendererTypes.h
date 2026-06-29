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
  uint32_t r7 = 0;
  uint32_t r8 = 0;
  uint32_t r9 = 0;
  uint32_t r10 = 0;
  uint32_t r28 = 0;
  uint32_t r29 = 0;
  uint32_t r30 = 0;
  uint32_t r31 = 0;
  uint32_t command_buffer_object = 0;
  uint32_t write_begin = 0;
  uint32_t write_end = 0;
  uint32_t write_limit_begin = 0;
  uint32_t write_limit_end = 0;
  uint32_t return_value = 0;
};

struct ShaderRecordProbeInfo {
  static constexpr std::size_t kMaxRecordDwords = 32;

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
  uint32_t r9 = 0;
  uint32_t r10 = 0;
  uint32_t r28 = 0;
  uint32_t r29 = 0;
  uint32_t r30 = 0;
  uint32_t r31 = 0;
  uint32_t command_buffer_object = 0;
  uint32_t write_begin = 0;
  uint32_t write_end = 0;
  uint32_t write_limit_begin = 0;
  uint32_t write_limit_end = 0;
  uint32_t return_value = 0;
  uint32_t primary_address = 0;
  uint32_t primary_dword_count_hint = 0;
  uint32_t primary_dword_count = 0;
  std::array<uint32_t, kMaxRecordDwords> primary_dwords{};
  bool primary_truncated = false;
  bool primary_missing = true;
  uint32_t secondary_address = 0;
  uint32_t secondary_dword_count = 0;
  std::array<uint32_t, kMaxRecordDwords> secondary_dwords{};
  bool secondary_truncated = false;
  bool secondary_missing = true;
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

struct VertexAttributeInfo {
  uint32_t data_format = 0;
  int32_t offset = 0;
  uint32_t stride = 0;
  int32_t exp_adjust = 0;
  uint32_t prefetch_count = 0;
  uint32_t signed_rf_mode = 0;
  bool is_index_rounded = false;
  bool is_signed = false;
  bool is_integer = false;
};

struct VertexFetchInfo {
  static constexpr std::size_t kMaxAttributes = 8;
  static constexpr std::size_t kMaxPayloadBytes = 512;

  uint32_t fetch_constant = 0;
  uint32_t dword_0 = 0;
  uint32_t dword_1 = 0;
  uint32_t type = 0;
  uint32_t address = 0;
  uint32_t size = 0;
  uint32_t endian = 0;
  uint32_t stride_words = 0;
  uint32_t attribute_count = 0;
  uint32_t captured_attribute_count = 0;
  std::array<VertexAttributeInfo, kMaxAttributes> attributes{};
  uint32_t payload_byte_count = 0;
  std::array<uint8_t, kMaxPayloadBytes> payload_bytes{};
  bool payload_truncated = false;
  bool payload_missing = true;
};

struct TextureFetchInfo {
  static constexpr std::size_t kMaxPayloadBytes = 4096;

  uint32_t shader_type = 0;
  uint32_t binding_index = 0;
  uint32_t fetch_constant = 0;
  std::array<uint32_t, 6> dwords{};
  uint32_t type = 0;
  uint32_t base_address = 0;
  uint32_t base_address_bytes = 0;
  uint32_t mip_address = 0;
  uint32_t mip_address_bytes = 0;
  uint32_t pitch = 0;
  bool tiled = false;
  uint32_t format = 0;
  uint32_t endian = 0;
  uint32_t request_size = 0;
  bool stacked = false;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth_or_stack = 0;
  uint32_t num_format = 0;
  uint32_t swizzle = 0;
  int32_t exp_adjust = 0;
  uint32_t mag_filter = 0;
  uint32_t min_filter = 0;
  uint32_t mip_filter = 0;
  uint32_t aniso_filter = 0;
  uint32_t arbitrary_filter = 0;
  uint32_t border_size = 0;
  uint32_t vol_mag_filter = 0;
  uint32_t vol_min_filter = 0;
  uint32_t mip_min_level = 0;
  uint32_t mip_max_level = 0;
  int32_t lod_bias = 0;
  int32_t grad_exp_adjust_h = 0;
  int32_t grad_exp_adjust_v = 0;
  uint32_t border_color = 0;
  uint32_t force_bc_w_to_max = 0;
  uint32_t tri_clamp = 0;
  int32_t aniso_bias = 0;
  uint32_t dimension = 0;
  bool packed_mips = false;
  uint32_t payload_byte_count = 0;
  std::array<uint8_t, kMaxPayloadBytes> payload_bytes{};
  bool payload_truncated = false;
  bool payload_missing = true;
};

struct RenderStateInfo {
  static constexpr std::size_t kColorTargetCount = 4;
  static constexpr std::size_t kViewportRegisterCount = 6;

  uint32_t rb_modecontrol = 0;
  uint32_t rb_surface_info = 0;
  uint32_t rb_colorcontrol = 0;
  uint32_t rb_color_mask = 0;
  uint32_t rb_depthcontrol = 0;
  uint32_t rb_stencilrefmask = 0;
  uint32_t rb_stencilrefmask_bf = 0;
  uint32_t rb_depth_info = 0;
  uint32_t rb_alpha_ref = 0;
  uint32_t pa_sc_screen_scissor_tl = 0;
  uint32_t pa_sc_screen_scissor_br = 0;
  uint32_t pa_sc_window_offset = 0;
  uint32_t pa_sc_window_scissor_tl = 0;
  uint32_t pa_sc_window_scissor_br = 0;
  uint32_t pa_cl_clip_cntl = 0;
  uint32_t pa_cl_vte_cntl = 0;
  uint32_t pa_su_sc_mode_cntl = 0;
  uint32_t pa_su_vtx_cntl = 0;
  uint32_t sq_program_cntl = 0;
  uint32_t sq_context_misc = 0;
  std::array<uint32_t, kViewportRegisterCount> viewport_registers{};
  std::array<uint32_t, kColorTargetCount> rb_color_info{};
  std::array<uint32_t, kColorTargetCount> rb_blendcontrol{};
  uint32_t surface_pitch = 0;
  uint32_t msaa_samples = 0;
  uint32_t depth_base = 0;
  uint32_t depth_format = 0;
  std::array<uint32_t, kColorTargetCount> color_base{};
  std::array<uint32_t, kColorTargetCount> color_format{};
  std::array<int32_t, kColorTargetCount> color_exp_bias{};
  bool depth_test_enable = false;
  bool depth_write_enable = false;
  bool stencil_enable = false;
  uint32_t depth_func = 0;
  uint32_t cull_mode = 0;
  uint32_t fill_mode = 0;
  uint32_t front_face = 0;
};

struct PM4DrawInfo {
  static constexpr std::size_t kMaxIndexPayloadBytes = 1024;
  static constexpr std::size_t kMaxVertexFetches = 32;
  static constexpr std::size_t kMaxTextureFetches = 64;

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
  uint32_t index_payload_byte_count = 0;
  std::array<uint8_t, kMaxIndexPayloadBytes> index_bytes{};
  bool index_payload_truncated = false;
  bool index_payload_missing = true;
  uint32_t vertex_fetch_count = 0;
  std::array<VertexFetchInfo, kMaxVertexFetches> vertex_fetches{};
  bool vertex_fetch_truncated = false;
  uint32_t texture_fetch_count = 0;
  std::array<TextureFetchInfo, kMaxTextureFetches> texture_fetches{};
  bool texture_fetch_truncated = false;
  RenderStateInfo render_state{};
  uint32_t major_mode = 0;
  bool explicit_major_mode = false;
  uint32_t viz_query_condition = 0;
  uint64_t vertex_shader_hash = 0;
  uint64_t pixel_shader_hash = 0;
};

struct PM4ShaderInfo {
  static constexpr std::size_t kMaxPayloadDwords = 512;

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
  uint32_t payload_dword_count = 0;
  std::array<uint32_t, kMaxPayloadDwords> payload_dwords{};
  bool payload_truncated = false;
  bool payload_missing = true;
};

struct PM4ConstantInfo {
  static constexpr std::size_t kMaxPayloadDwords = 64;

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
  uint32_t payload_dword_count = 0;
  std::array<uint32_t, kMaxPayloadDwords> payload_dwords{};
  bool payload_truncated = false;
  bool payload_missing = true;
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

const char *ToString(RendererMode mode);
const char *ToString(RendererBackendKind backend);
RendererMode ParseRendererMode(std::string mode);

} // namespace bo2::native
