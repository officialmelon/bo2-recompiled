#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bo2::native::replay {

enum class CaptureEventType {
  Unknown,
  CaptureStart,
  BeginFrame,
  EndFrame,
  VdSwap,
  PresentSnapshot,
  DrawPacketCandidate,
  ShaderRecordProbe,
  PM4Packet,
  PM4Draw,
  PM4Shader,
  PM4Constants,
  PM4Swap,
  RenderCommand,
};

struct PM4PacketRecord {
  uint64_t event = 0;
  uint32_t opcode = 0;
  uint32_t packet = 0;
  uint32_t payload_dword_count = 0;
  uint32_t packet_ptr = 0;
  uint32_t buffer_ptr = 0;
  uint32_t packet_offset = 0;
  std::vector<uint32_t> first_dwords;
};

struct PM4ShaderRecord {
  uint64_t event = 0;
  uint32_t opcode = 0;
  uint32_t packet = 0;
  uint32_t packet_ptr = 0;
  uint32_t buffer_ptr = 0;
  uint32_t packet_offset = 0;
  uint32_t shader_type = 0;
  bool embedded = false;
  uint32_t guest_address = 0;
  uint64_t host_address = 0;
  uint32_t dword_count = 0;
  uint64_t shader_hash = 0;
  uint32_t payload_dword_count = 0;
  std::vector<uint32_t> dwords;
  bool payload_truncated = false;
  bool payload_missing = true;
};

struct PM4ConstantRecord {
  uint64_t seq = 0;
  uint64_t event = 0;
  uint32_t opcode = 0;
  uint32_t packet = 0;
  uint32_t packet_ptr = 0;
  uint32_t buffer_ptr = 0;
  uint32_t packet_offset = 0;
  uint32_t address = 0;
  uint32_t offset_type = 0;
  uint32_t constant_type = 0;
  uint32_t index = 0;
  uint32_t dword_count = 0;
  uint32_t payload_dword_count = 0;
  std::vector<uint32_t> dwords;
  bool payload_truncated = false;
  bool payload_missing = true;
};

struct VertexAttributeRecord {
  uint32_t data_format = 0;
  int32_t offset = 0;
  uint32_t offset_bytes = 0;
  uint32_t stride = 0;
  uint32_t stride_bytes = 0;
  int32_t exp_adjust = 0;
  uint32_t prefetch_count = 0;
  uint32_t signed_rf_mode = 0;
  bool is_index_rounded = false;
  bool is_signed = false;
  bool is_integer = false;
};

struct VertexFetchRecord {
  uint32_t fetch_constant = 0;
  uint32_t dword_0 = 0;
  uint32_t dword_1 = 0;
  uint32_t type = 0;
  uint32_t address = 0;
  uint32_t address_bytes = 0;
  uint32_t size_words = 0;
  uint32_t size_bytes = 0;
  uint32_t endian = 0;
  uint32_t stride_words = 0;
  uint32_t stride_bytes = 0;
  uint32_t attribute_count = 0;
  uint32_t captured_attribute_count = 0;
  std::vector<VertexAttributeRecord> attributes;
  uint32_t payload_byte_count = 0;
  uint32_t payload_resource_byte_count = 0;
  std::string payload_resource_path;
  std::vector<uint8_t> payload_bytes;
  bool payload_truncated = false;
  bool payload_missing = true;
  bool payload_loaded_from_resource = false;
};

struct TextureFetchRecord {
  uint32_t shader_type = 0;
  uint32_t binding_index = 0;
  uint32_t fetch_constant = 0;
  std::vector<uint32_t> dwords;
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
  uint32_t clamp_x = 0;
  uint32_t clamp_y = 0;
  uint32_t clamp_z = 0;
  bool clamp_modes_present = false;
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
  uint32_t payload_resource_byte_count = 0;
  std::string payload_resource_path;
  std::vector<uint8_t> payload_bytes;
  bool payload_truncated = false;
  bool payload_missing = true;
  bool payload_loaded_from_resource = false;
};

struct RenderTargetPayloadRecord {
  uint32_t target = 0;
  uint32_t base = 0;
  uint32_t payload_requested_byte_count = 0;
  uint32_t payload_offset_bytes = 0;
  uint32_t payload_byte_count = 0;
  uint32_t payload_resource_byte_count = 0;
  std::string payload_resource_path;
  std::vector<uint8_t> payload_bytes;
  bool payload_truncated = false;
  bool payload_missing = true;
  bool payload_loaded_from_resource = false;
};

struct RenderStateRecord {
  bool present = false;
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
  std::vector<uint32_t> viewport_registers;
  std::vector<uint32_t> rb_color_info;
  std::vector<uint32_t> rb_blendcontrol;
  uint32_t surface_pitch = 0;
  uint32_t msaa_samples = 0;
  uint32_t depth_base = 0;
  uint32_t depth_format = 0;
  std::vector<uint32_t> color_base;
  std::vector<uint32_t> color_format;
  std::vector<int32_t> color_exp_bias;
  std::vector<RenderTargetPayloadRecord> color_target_payloads;
  RenderTargetPayloadRecord depth_target_payload;
  bool depth_test_enable = false;
  bool depth_write_enable = false;
  bool stencil_enable = false;
  uint32_t depth_func = 0;
  uint32_t cull_mode = 0;
  uint32_t fill_mode = 0;
  uint32_t front_face = 0;
};

struct PM4DrawRecord {
  uint64_t event = 0;
  std::string opcode_name;
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
  std::vector<uint8_t> index_bytes;
  bool index_payload_truncated = false;
  bool index_payload_missing = true;
  uint32_t vertex_fetch_count = 0;
  std::vector<VertexFetchRecord> vertex_fetches;
  bool vertex_fetch_truncated = false;
  bool vertex_fetch_state_present = false;
  uint32_t texture_fetch_count = 0;
  std::vector<TextureFetchRecord> texture_fetches;
  bool texture_fetch_truncated = false;
  bool texture_fetch_state_present = false;
  RenderStateRecord render_state;
  uint32_t major_mode = 0;
  bool explicit_major_mode = false;
  uint32_t viz_query_condition = 0;
  uint64_t vertex_shader_hash = 0;
  uint64_t pixel_shader_hash = 0;
};

struct PM4SwapRecord {
  uint64_t event = 0;
  uint32_t opcode = 0;
  uint32_t packet = 0;
  uint32_t packet_ptr = 0;
  uint32_t buffer_ptr = 0;
  uint32_t packet_offset = 0;
  uint32_t frontbuffer_ptr = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t frame_counter = 0;
  uint32_t frontbuffer_payload_requested_byte_count = 0;
  uint32_t frontbuffer_payload_byte_count = 0;
  uint32_t frontbuffer_payload_resource_byte_count = 0;
  std::string frontbuffer_payload_resource_path;
  std::vector<uint8_t> frontbuffer_payload_bytes;
  bool frontbuffer_payload_truncated = false;
  bool frontbuffer_payload_missing = true;
  bool frontbuffer_payload_loaded_from_resource = false;
  bool frontbuffer_fetch_valid = false;
  TextureFetchRecord frontbuffer_fetch;
};

struct RenderCommandRecord {
  uint64_t sequence = 0;
  std::string command;
  uint32_t guest_address = 0;
  uint32_t arg0 = 0;
  uint32_t arg1 = 0;
  uint32_t arg2 = 0;
  uint32_t arg3 = 0;
};

struct VdSwapRecord {
  uint64_t frame = 0;
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

struct PresentSnapshotRecord {
  uint64_t frame = 0;
  uint32_t command_buffer = 0;
  uint32_t dword_count = 0;
  bool has_xe_swap = false;
  uint32_t xe_swap_dword_offset = 0;
  uint32_t xe_swap_packet = 0;
  uint32_t swap_signature = 0;
  uint32_t frontbuffer_physical = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint32_t> dwords;
};

struct DrawPacketCandidateRecord {
  uint64_t event = 0;
  std::string function;
  uint32_t function_address = 0;
  uint64_t link_register = 0;
  uint32_t command_buffer_object = 0;
  uint32_t write_begin = 0;
  uint32_t write_end = 0;
  uint32_t write_limit = 0;
  uint32_t dword_count = 0;
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

struct ShaderRecordProbeRecord {
  uint64_t event = 0;
  std::string function;
  uint32_t function_address = 0;
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
  std::vector<uint32_t> primary_dwords;
  bool primary_truncated = false;
  bool primary_missing = true;
  uint32_t secondary_address = 0;
  uint32_t secondary_dword_count = 0;
  std::vector<uint32_t> secondary_dwords;
  bool secondary_truncated = false;
  bool secondary_missing = true;
};

struct CaptureEvent {
  uint64_t seq = 0;
  uint64_t line = 0;
  CaptureEventType type = CaptureEventType::Unknown;
  std::string type_name;
  std::optional<uint64_t> frame;
  PM4PacketRecord packet;
  PM4ShaderRecord shader;
  PM4ConstantRecord constants;
  PM4DrawRecord draw;
  PM4SwapRecord swap;
  RenderCommandRecord command;
  VdSwapRecord vd_swap;
  PresentSnapshotRecord present;
  DrawPacketCandidateRecord draw_candidate;
  ShaderRecordProbeRecord shader_probe;
};

struct BoundShaderState {
  uint64_t hash = 0;
  uint32_t guest_address = 0;
  uint32_t dword_count = 0;
  uint64_t seq = 0;
};

struct ReplayDrawState {
  std::size_t draw_index = 0;
  std::size_t event_index = 0;
  uint64_t seq = 0;
  std::optional<std::size_t> frame_index;
  std::optional<uint64_t> frame_id;
  PM4DrawRecord draw;
  BoundShaderState vertex_shader;
  BoundShaderState pixel_shader;
  uint64_t constants_seen_total = 0;
  uint64_t constants_seen_in_frame = 0;
  uint64_t last_constant_seq = 0;
  std::vector<PM4ConstantRecord> recent_constants;
  std::vector<PM4ConstantRecord> bound_constants;
  bool missing_vertex_shader = false;
  bool missing_pixel_shader = false;
  bool missing_constants = false;
};

struct ReplayFrame {
  std::size_t index = 0;
  uint64_t frame_id = 0;
  uint64_t begin_seq = 0;
  uint64_t end_seq = 0;
  bool has_begin = false;
  bool has_end = false;
  std::map<CaptureEventType, uint64_t> event_counts;
  std::size_t first_draw_index = 0;
  std::size_t draw_count = 0;
  std::size_t shader_count = 0;
  std::size_t constant_count = 0;
  std::size_t swap_count = 0;
};

struct ShaderUsageRecord {
  uint64_t hash = 0;
  uint32_t shader_type = 0;
  uint64_t loads = 0;
  uint64_t draws = 0;
  uint32_t max_dwords = 0;
};

struct ShaderPairUsageRecord {
  uint64_t vertex_hash = 0;
  uint64_t pixel_hash = 0;
  uint64_t draws = 0;
};

struct ReplaySummary {
  uint64_t total_events = 0;
  uint64_t parse_errors = 0;
  uint64_t pre_frame_events = 0;
  uint64_t missing_vertex_shader_draws = 0;
  uint64_t missing_pixel_shader_draws = 0;
  uint64_t missing_constant_draws = 0;
  uint64_t constant_uploads_with_payload = 0;
  uint64_t constant_uploads_missing_payload = 0;
  uint64_t constant_payload_dwords = 0;
  uint64_t constant_payload_truncated = 0;
  uint64_t shader_uploads_with_payload = 0;
  uint64_t shader_uploads_missing_payload = 0;
  uint64_t shader_payload_dwords = 0;
  uint64_t shader_payload_truncated = 0;
  uint64_t index_buffer_snapshots = 0;
  uint64_t index_buffer_snapshots_missing = 0;
  uint64_t index_payload_bytes = 0;
  uint64_t index_payload_truncated = 0;
  uint64_t draws_with_vertex_fetch = 0;
  uint64_t draws_missing_vertex_fetch = 0;
  uint64_t draws_missing_vertex_fetch_state = 0;
  uint64_t vertex_fetch_records = 0;
  uint64_t vertex_buffer_snapshots = 0;
  uint64_t vertex_buffer_snapshots_missing = 0;
  uint64_t vertex_payload_bytes = 0;
  uint64_t vertex_payload_truncated = 0;
  uint64_t draws_with_texture_fetch = 0;
  uint64_t draws_missing_texture_fetch = 0;
  uint64_t draws_missing_texture_fetch_state = 0;
  uint64_t texture_fetch_records = 0;
  uint64_t texture_fetches_with_clamp_modes = 0;
  uint64_t texture_fetches_missing_clamp_modes = 0;
  uint64_t texture_snapshots = 0;
  uint64_t texture_snapshots_missing = 0;
  uint64_t texture_payload_bytes = 0;
  uint64_t texture_payload_sidecars = 0;
  uint64_t texture_payload_sidecar_bytes = 0;
  uint64_t texture_payload_truncated = 0;
  uint64_t frontbuffer_snapshots = 0;
  uint64_t frontbuffer_snapshots_missing = 0;
  uint64_t frontbuffer_payload_bytes = 0;
  uint64_t frontbuffer_payload_sidecars = 0;
  uint64_t frontbuffer_payload_sidecar_bytes = 0;
  uint64_t frontbuffer_payload_truncated = 0;
  uint64_t color_target_snapshots = 0;
  uint64_t color_target_snapshots_missing = 0;
  uint64_t color_target_payload_bytes = 0;
  uint64_t color_target_payload_sidecars = 0;
  uint64_t color_target_payload_sidecar_bytes = 0;
  uint64_t color_target_payload_truncated = 0;
  uint64_t depth_target_snapshots = 0;
  uint64_t depth_target_snapshots_missing = 0;
  uint64_t depth_target_payload_bytes = 0;
  uint64_t depth_target_payload_sidecars = 0;
  uint64_t depth_target_payload_sidecar_bytes = 0;
  uint64_t depth_target_payload_truncated = 0;
  uint64_t draws_with_render_state = 0;
  uint64_t draws_missing_render_state = 0;
  std::map<CaptureEventType, uint64_t> event_counts;
  std::map<uint32_t, uint64_t> draw_opcode_counts;
  std::map<uint32_t, uint64_t> primitive_counts;
  std::map<uint32_t, uint64_t> source_select_counts;
};

struct ReplayCapture {
  std::filesystem::path path;
  ReplaySummary summary;
  std::vector<CaptureEvent> events;
  std::vector<ReplayFrame> frames;
  std::vector<ReplayDrawState> draws;
  std::vector<std::string> warnings;
  std::vector<std::string> errors;
  std::vector<ShaderUsageRecord> shader_usage;
  std::vector<ShaderPairUsageRecord> shader_pair_usage;
};

struct ReplayLoadOptions {
  bool keep_events = true;
  std::size_t max_warnings = 200;
};

struct ReplayCliOptions {
  std::filesystem::path capture_path;
  std::filesystem::path d3d12_output_path;
  std::filesystem::path d3d12_depth_output_path;
  std::filesystem::path frontbuffer_output_path;
  std::filesystem::path texture_output_path;
  std::filesystem::path shader_override_root =
      std::filesystem::path("shader_work") / "native_overrides";
  std::filesystem::path shader_cache_root =
      std::filesystem::path("shader_work") / "cache";
  bool show_summary = true;
  bool dump_draws = false;
  bool dump_constants = false;
  bool dump_bound_state = false;
  bool dump_indices = false;
  bool dump_vertices = false;
  bool dump_frontbuffer = false;
  bool dump_texture = false;
  bool show_resource_summary = false;
  bool show_shader_usage = false;
  bool show_missing_shaders = false;
  bool show_shader_record_probes = false;
  bool show_real_backend_gaps = false;
  bool validate_only = false;
  bool allow_diagnostic_shader = false;
  bool strict_frame_replay = false;
  bool skip_unsupported = false;
#if defined(_WIN32)
  bool live_submit = false;
  struct D3D12LiveSubmitBinding *live_binding = nullptr;
  struct D3D12LiveReplaySession *live_session = nullptr;
#endif
  std::optional<std::size_t> frame_index;
  std::optional<std::size_t> draw_index;
  std::optional<std::size_t> frontbuffer_index;
  std::optional<std::size_t> texture_slot;
  std::size_t max_draws = 64;
  std::size_t top_shaders = 20;
  std::size_t d3d12_draw_limit = 4096;
  std::string backend = "null";
};

struct ShaderOverrideRecord {
  std::string backend;
  std::string stage;
  uint64_t runtime_hash = 0;
  std::string entry;
  std::string profile;
  std::filesystem::path path;
  std::string source;
};

struct ShaderCacheRecord {
  std::string backend;
  std::string stage;
  uint64_t runtime_hash = 0;
  std::string entry;
  std::string profile;
  std::string compiler;
  std::string format;
  std::string cache_key;
  std::filesystem::path path;
  std::filesystem::path source;
  bool diagnostic = false;
};

const char *ToString(CaptureEventType type);
CaptureEventType ParseCaptureEventType(std::string_view type);

bool LoadReplayCapture(const std::filesystem::path &path,
                       const ReplayLoadOptions &options,
                       ReplayCapture &capture);

std::string FormatHex32(uint32_t value);
std::string FormatHex64(uint64_t value);

bool LoadShaderOverrideManifest(const std::filesystem::path &path,
                                std::vector<ShaderOverrideRecord> &records,
                                std::string &error);
bool LoadShaderCacheIndex(const std::filesystem::path &path,
                          std::vector<ShaderCacheRecord> &records,
                          std::string &error);

void PrintReplaySummary(const ReplayCapture &capture);
void PrintFrameSummary(const ReplayCapture &capture, std::size_t frame_index);
void PrintDrawDump(const ReplayCapture &capture,
                   std::optional<std::size_t> frame_index,
                   std::optional<std::size_t> draw_index,
                   std::size_t max_draws);
void PrintConstantsDump(const ReplayCapture &capture,
                        std::optional<std::size_t> frame_index,
                        std::optional<std::size_t> draw_index);
void PrintBoundStateDump(const ReplayCapture &capture, std::size_t draw_index);
void PrintIndexDump(const ReplayCapture &capture, std::size_t draw_index);
void PrintVertexDump(const ReplayCapture &capture, std::size_t draw_index);
void PrintResourceSummary(const ReplayCapture &capture);
void PrintShaderUsage(const ReplayCapture &capture, std::size_t top_count);
void PrintMissingShaders(const ReplayCapture &capture);
void PrintShaderRecordProbes(const ReplayCapture &capture,
                             std::size_t max_count);

bool RunD3D12DiagnosticReplayBackend(const ReplayCapture &capture,
                                     const ReplayCliOptions &options,
                                     std::string &error);
bool RunD3D12RealReplayBackend(const ReplayCapture &capture,
                               const ReplayCliOptions &options,
                               std::string &error);
bool DumpD3D12DecodedTexturePreview(const ReplayCapture &capture,
                                    const ReplayCliOptions &options,
                                    std::string &error);
void PrintD3D12RealBackendGaps(const ReplayCapture &capture,
                               const ReplayCliOptions &options);

int RunNativeRenderReplayTool(int argc, char **argv);

} // namespace bo2::native::replay
