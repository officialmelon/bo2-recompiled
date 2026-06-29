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
  std::vector<uint8_t> payload_bytes;
  bool payload_truncated = false;
  bool payload_missing = true;
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
  std::filesystem::path shader_override_root =
      std::filesystem::path("shader_work") / "native_overrides";
  bool show_summary = true;
  bool dump_draws = false;
  bool dump_constants = false;
  bool dump_bound_state = false;
  bool dump_indices = false;
  bool dump_vertices = false;
  bool show_resource_summary = false;
  bool show_shader_usage = false;
  bool show_missing_shaders = false;
  bool show_shader_record_probes = false;
  bool validate_only = false;
  bool allow_diagnostic_shader = false;
  std::optional<std::size_t> frame_index;
  std::optional<std::size_t> draw_index;
  std::size_t max_draws = 64;
  std::size_t top_shaders = 20;
  std::size_t d3d12_draw_limit = 4096;
  std::string backend = "null";
};

const char *ToString(CaptureEventType type);
CaptureEventType ParseCaptureEventType(std::string_view type);

bool LoadReplayCapture(const std::filesystem::path &path,
                       const ReplayLoadOptions &options,
                       ReplayCapture &capture);

std::string FormatHex32(uint32_t value);
std::string FormatHex64(uint64_t value);

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

int RunNativeRenderReplayTool(int argc, char **argv);

} // namespace bo2::native::replay
