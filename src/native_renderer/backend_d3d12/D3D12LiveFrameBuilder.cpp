#include "D3D12LiveFrameBuilder.h"

#include <algorithm>
#include <utility>

namespace bo2::native {
namespace {

replay::VertexAttributeRecord ConvertAttribute(const VertexAttributeInfo &info) {
  replay::VertexAttributeRecord out{};
  out.data_format = info.data_format;
  out.offset = info.offset;
  out.offset_bytes = static_cast<uint32_t>(info.offset) * 4u;
  out.stride = info.stride;
  out.stride_bytes = info.stride * 4u;
  out.exp_adjust = info.exp_adjust;
  out.prefetch_count = info.prefetch_count;
  out.signed_rf_mode = info.signed_rf_mode;
  out.is_index_rounded = info.is_index_rounded;
  out.is_signed = info.is_signed;
  out.is_integer = info.is_integer;
  return out;
}

replay::VertexFetchRecord ConvertVertexFetch(const VertexFetchInfo &fetch) {
  replay::VertexFetchRecord out{};
  out.fetch_constant = fetch.fetch_constant;
  out.dword_0 = fetch.dword_0;
  out.dword_1 = fetch.dword_1;
  out.type = fetch.type;
  out.address = fetch.address;
  out.address_bytes = fetch.address << 2;
  out.size_words = fetch.size;
  out.size_bytes = fetch.size << 2;
  out.endian = fetch.endian;
  out.stride_words = fetch.stride_words;
  out.stride_bytes = fetch.stride_words << 2;
  out.attribute_count = fetch.attribute_count;
  out.captured_attribute_count = fetch.captured_attribute_count;
  out.attributes.reserve(fetch.captured_attribute_count);
  for (uint32_t i = 0; i < fetch.captured_attribute_count &&
                      i < fetch.attributes.size();
       ++i) {
    out.attributes.push_back(ConvertAttribute(fetch.attributes[i]));
  }
  out.payload_byte_count = fetch.payload_byte_count;
  out.payload_bytes.assign(fetch.payload_bytes.begin(), fetch.payload_bytes.end());
  out.payload_truncated = fetch.payload_truncated;
  out.payload_missing = fetch.payload_missing;
  return out;
}

replay::TextureFetchRecord ConvertTextureFetch(const TextureFetchInfo &fetch) {
  replay::TextureFetchRecord out{};
  out.shader_type = fetch.shader_type;
  out.binding_index = fetch.binding_index;
  out.fetch_constant = fetch.fetch_constant;
  out.dwords.assign(fetch.dwords.begin(), fetch.dwords.end());
  out.type = fetch.type;
  out.base_address = fetch.base_address;
  out.base_address_bytes = fetch.base_address_bytes;
  out.mip_address = fetch.mip_address;
  out.mip_address_bytes = fetch.mip_address_bytes;
  out.pitch = fetch.pitch;
  out.tiled = fetch.tiled;
  out.format = fetch.format;
  out.endian = fetch.endian;
  out.request_size = fetch.request_size;
  out.stacked = fetch.stacked;
  out.width = fetch.width;
  out.height = fetch.height;
  out.depth_or_stack = fetch.depth_or_stack;
  out.num_format = fetch.num_format;
  out.swizzle = fetch.swizzle;
  out.exp_adjust = fetch.exp_adjust;
  out.clamp_x = fetch.clamp_x;
  out.clamp_y = fetch.clamp_y;
  out.clamp_z = fetch.clamp_z;
  out.mag_filter = fetch.mag_filter;
  out.min_filter = fetch.min_filter;
  out.mip_filter = fetch.mip_filter;
  out.aniso_filter = fetch.aniso_filter;
  out.arbitrary_filter = fetch.arbitrary_filter;
  out.border_size = fetch.border_size;
  out.vol_mag_filter = fetch.vol_mag_filter;
  out.vol_min_filter = fetch.vol_min_filter;
  out.mip_min_level = fetch.mip_min_level;
  out.mip_max_level = fetch.mip_max_level;
  out.lod_bias = fetch.lod_bias;
  out.grad_exp_adjust_h = fetch.grad_exp_adjust_h;
  out.grad_exp_adjust_v = fetch.grad_exp_adjust_v;
  out.border_color = fetch.border_color;
  out.force_bc_w_to_max = fetch.force_bc_w_to_max;
  out.tri_clamp = fetch.tri_clamp;
  out.aniso_bias = fetch.aniso_bias;
  out.dimension = fetch.dimension;
  out.packed_mips = fetch.packed_mips;
  out.payload_byte_count = fetch.payload_byte_count;
  out.payload_bytes.assign(fetch.payload_bytes.begin(), fetch.payload_bytes.end());
  out.payload_truncated = fetch.payload_truncated;
  out.payload_missing = fetch.payload_missing;
  out.clamp_modes_present = true;
  return out;
}

replay::RenderStateRecord ConvertRenderState(const RenderStateInfo &state) {
  replay::RenderStateRecord out{};
  out.present = true;
  out.rb_modecontrol = state.rb_modecontrol;
  out.rb_surface_info = state.rb_surface_info;
  out.rb_colorcontrol = state.rb_colorcontrol;
  out.rb_color_mask = state.rb_color_mask;
  out.rb_depthcontrol = state.rb_depthcontrol;
  out.rb_stencilrefmask = state.rb_stencilrefmask;
  out.rb_stencilrefmask_bf = state.rb_stencilrefmask_bf;
  out.rb_depth_info = state.rb_depth_info;
  out.rb_alpha_ref = state.rb_alpha_ref;
  out.rb_copy_control = state.rb_copy_control;
  out.rb_copy_dest_base = state.rb_copy_dest_base;
  out.rb_copy_dest_pitch = state.rb_copy_dest_pitch;
  out.rb_copy_dest_info = state.rb_copy_dest_info;
  out.rb_depth_clear = state.rb_depth_clear;
  out.rb_color_clear = state.rb_color_clear;
  out.rb_color_clear_lo = state.rb_color_clear_lo;
  out.pa_sc_screen_scissor_tl = state.pa_sc_screen_scissor_tl;
  out.pa_sc_screen_scissor_br = state.pa_sc_screen_scissor_br;
  out.pa_sc_window_offset = state.pa_sc_window_offset;
  out.pa_sc_window_scissor_tl = state.pa_sc_window_scissor_tl;
  out.pa_sc_window_scissor_br = state.pa_sc_window_scissor_br;
  out.pa_cl_clip_cntl = state.pa_cl_clip_cntl;
  out.pa_cl_vte_cntl = state.pa_cl_vte_cntl;
  out.pa_su_sc_mode_cntl = state.pa_su_sc_mode_cntl;
  out.pa_su_vtx_cntl = state.pa_su_vtx_cntl;
  out.sq_program_cntl = state.sq_program_cntl;
  out.sq_context_misc = state.sq_context_misc;
  out.viewport_registers.assign(state.viewport_registers.begin(),
                                state.viewport_registers.end());
  out.rb_color_info.assign(state.rb_color_info.begin(),
                           state.rb_color_info.end());
  out.rb_blendcontrol.assign(state.rb_blendcontrol.begin(),
                             state.rb_blendcontrol.end());
  out.rb_blend_factor.assign(state.rb_blend_factor.begin(),
                             state.rb_blend_factor.end());
  out.surface_pitch = state.surface_pitch;
  out.msaa_samples = state.msaa_samples;
  out.depth_base = state.depth_base;
  out.depth_format = state.depth_format;
  out.color_base.assign(state.color_base.begin(), state.color_base.end());
  out.color_format.assign(state.color_format.begin(), state.color_format.end());
  out.color_exp_bias.assign(state.color_exp_bias.begin(),
                            state.color_exp_bias.end());
  out.depth_test_enable = state.depth_test_enable;
  out.depth_write_enable = state.depth_write_enable;
  out.stencil_enable = state.stencil_enable;
  out.depth_func = state.depth_func;
  out.cull_mode = state.cull_mode;
  out.fill_mode = state.fill_mode;
  out.front_face = state.front_face;
  return out;
}

replay::PM4DrawRecord ConvertDraw(const PM4DrawInfo &draw) {
  replay::PM4DrawRecord out{};
  out.event = draw.event_index;
  out.opcode_name = std::string(draw.opcode_name);
  out.opcode = draw.opcode;
  out.packet = draw.packet;
  out.packet_ptr = draw.packet_ptr;
  out.buffer_ptr = draw.buffer_ptr;
  out.packet_offset = draw.packet_offset;
  out.index_count = draw.index_count;
  out.primitive_type = draw.primitive_type;
  out.source_select = draw.source_select;
  out.indexed = draw.indexed;
  out.index_base = draw.index_base;
  out.index_length = draw.index_length;
  out.index_buffer_count = draw.index_buffer_count;
  out.index_format = draw.index_format;
  out.index_endianness = draw.index_endianness;
  out.index_payload_byte_count = draw.index_payload_byte_count;
  out.index_bytes.assign(draw.index_bytes.begin(),
                         draw.index_bytes.begin() + draw.index_payload_byte_count);
  out.index_payload_truncated = draw.index_payload_truncated;
  out.index_payload_missing = draw.index_payload_missing;
  out.vertex_fetch_count = draw.vertex_fetch_count;
  out.vertex_fetch_truncated = draw.vertex_fetch_truncated;
  out.vertex_fetch_state_present = draw.vertex_fetch_count > 0;
  const uint32_t vertex_fetch_count = std::min<uint32_t>(
      draw.vertex_fetch_count, static_cast<uint32_t>(draw.vertex_fetches.size()));
  out.vertex_fetches.reserve(vertex_fetch_count);
  for (uint32_t i = 0; i < vertex_fetch_count; ++i) {
    out.vertex_fetches.push_back(ConvertVertexFetch(draw.vertex_fetches[i]));
  }
  out.texture_fetch_count = draw.texture_fetch_count;
  out.texture_fetch_truncated = draw.texture_fetch_truncated;
  out.texture_fetch_state_present = draw.texture_fetch_count > 0;
  const uint32_t texture_fetch_count = std::min<uint32_t>(
      draw.texture_fetch_count, static_cast<uint32_t>(draw.texture_fetches.size()));
  out.texture_fetches.reserve(texture_fetch_count);
  for (uint32_t i = 0; i < texture_fetch_count; ++i) {
    out.texture_fetches.push_back(ConvertTextureFetch(draw.texture_fetches[i]));
  }
  out.render_state = ConvertRenderState(draw.render_state);
  // The translated d3d12-xenia live pipeline reads the guest float constant
  // register file per draw; without this copy the live constant buffers are
  // all zero and every constant-driven shader renders black.
  out.float_constant_dword_count = std::min<uint32_t>(
      draw.float_constant_dword_count,
      uint32_t(PM4DrawInfo::kFloatConstantDwordCount));
  out.float_constant_dwords.assign(
      draw.float_constant_dwords.begin(),
      draw.float_constant_dwords.begin() + out.float_constant_dword_count);
  out.float_constants_missing = draw.float_constants_missing;
  out.major_mode = draw.major_mode;
  out.explicit_major_mode = draw.explicit_major_mode;
  out.viz_query_condition = draw.viz_query_condition;
  out.vertex_shader_hash = draw.vertex_shader_hash;
  out.pixel_shader_hash = draw.pixel_shader_hash;
  return out;
}

}  // namespace

void D3D12LiveFrameBuilder::BeginFrame(uint64_t frame_index) {
  frame_index_ = frame_index;
  // Shader and constant registers are persistent GPU state. BO2 often updates
  // only the ranges that changed for a frame, so clearing them at swap
  // boundaries makes live native rendering lose atlas/animation constants and
  // causes intermittent no-output draws.
  constants_seen_in_frame_ = 0;
  recent_constants_.clear();
  draws_.clear();
}

void D3D12LiveFrameBuilder::ResetState() {
  frame_index_ = 0;
  vertex_shader_ = {};
  pixel_shader_ = {};
  constants_seen_total_ = 0;
  constants_seen_in_frame_ = 0;
  last_constant_seq_ = 0;
  recent_constants_.clear();
  bound_constants_.clear();
  draws_.clear();
}

void D3D12LiveFrameBuilder::AbsorbPending(D3D12LiveFrameBuilder& pending,
                                          uint64_t frame_index) {
  frame_index_ = frame_index;
  if (pending.vertex_shader_.hash != 0) {
    vertex_shader_ = pending.vertex_shader_;
  }
  if (pending.pixel_shader_.hash != 0) {
    pixel_shader_ = pending.pixel_shader_;
  }
  constants_seen_total_ += pending.constants_seen_total_;
  constants_seen_in_frame_ += pending.constants_seen_total_;
  if (pending.last_constant_seq_ != 0) {
    last_constant_seq_ = pending.last_constant_seq_;
  }
  recent_constants_.insert(recent_constants_.end(),
                           pending.recent_constants_.begin(),
                           pending.recent_constants_.end());
  if (recent_constants_.size() > 8) {
    recent_constants_.erase(recent_constants_.begin(),
                            recent_constants_.end() - 8);
  }
  for (const auto &[key, constant] : pending.bound_constants_) {
    bound_constants_[key] = constant;
  }

  draws_.reserve(draws_.size() + pending.draws_.size());
  for (replay::ReplayDrawState draw : pending.draws_) {
    draw.draw_index = draws_.size();
    draw.frame_index = 0;
    draw.frame_id = frame_index;
    draws_.push_back(std::move(draw));
  }
  pending.ResetState();
}

void D3D12LiveFrameBuilder::BindShader(const PM4ShaderInfo &shader,
                                         uint64_t seq) {
  replay::BoundShaderState bound{};
  bound.hash = shader.shader_hash;
  bound.guest_address = shader.guest_address;
  bound.dword_count = shader.dword_count;
  bound.seq = seq;
  if (shader.shader_type == 0) {
    vertex_shader_ = bound;
  } else if (shader.shader_type == 1) {
    pixel_shader_ = bound;
  }
}

void D3D12LiveFrameBuilder::BindConstants(const PM4ConstantInfo &constants,
                                          uint64_t seq) {
  replay::PM4ConstantRecord record{};
  record.seq = seq;
  record.event = constants.event_index;
  record.opcode = constants.opcode;
  record.packet = constants.packet;
  record.packet_ptr = constants.packet_ptr;
  record.buffer_ptr = constants.buffer_ptr;
  record.packet_offset = constants.packet_offset;
  record.address = constants.address;
  record.offset_type = constants.offset_type;
  record.constant_type = constants.type;
  record.index = constants.index;
  record.dword_count = constants.dword_count;
  record.payload_dword_count = constants.payload_dword_count;
  record.dwords.assign(
      constants.payload_dwords.begin(),
      constants.payload_dwords.begin() + constants.payload_dword_count);
  record.payload_truncated = constants.payload_truncated;
  record.payload_missing = constants.payload_missing;

  ++constants_seen_total_;
  ++constants_seen_in_frame_;
  last_constant_seq_ = seq;
  recent_constants_.push_back(record);
  bound_constants_[{record.constant_type, record.index}] = record;
  if (recent_constants_.size() > 8) {
    recent_constants_.erase(recent_constants_.begin());
  }
}

void D3D12LiveFrameBuilder::AddDraw(const PM4DrawInfo &draw, uint64_t seq) {
  replay::ReplayDrawState draw_state{};
  draw_state.draw_index = draws_.size();
  draw_state.event_index = draw.event_index;
  draw_state.seq = seq;
  draw_state.frame_index = 0;
  draw_state.frame_id = frame_index_;
  draw_state.draw = ConvertDraw(draw);
  draw_state.vertex_shader = vertex_shader_;
  draw_state.pixel_shader = pixel_shader_;
  if (draw.vertex_shader_hash != 0) {
    draw_state.vertex_shader.hash = draw.vertex_shader_hash;
    draw_state.vertex_shader.seq = seq;
  }
  if (draw.pixel_shader_hash != 0) {
    draw_state.pixel_shader.hash = draw.pixel_shader_hash;
    draw_state.pixel_shader.seq = seq;
  }
  draw_state.constants_seen_total = constants_seen_total_;
  draw_state.constants_seen_in_frame = constants_seen_in_frame_;
  draw_state.last_constant_seq = last_constant_seq_;
  draw_state.recent_constants = recent_constants_;
  draw_state.bound_constants.reserve(bound_constants_.size());
  for (const auto &[key, constant] : bound_constants_) {
    (void)key;
    draw_state.bound_constants.push_back(constant);
  }
  draw_state.missing_vertex_shader = draw_state.vertex_shader.hash == 0;
  draw_state.missing_pixel_shader = draw_state.pixel_shader.hash == 0;
  draw_state.missing_constants = constants_seen_total_ == 0;
  draws_.push_back(std::move(draw_state));
}

replay::ReplayCapture D3D12LiveFrameBuilder::BuildCapture(
    uint64_t frame_index) const {
  replay::ReplayCapture capture{};
  capture.path = "live_frame";
  replay::ReplayFrame frame{};
  frame.index = 0;
  frame.frame_id = frame_index;
  frame.begin_seq = draws_.empty() ? 0 : draws_.front().seq;
  frame.end_seq = draws_.empty() ? 0 : draws_.back().seq;
  frame.has_begin = true;
  frame.has_end = true;
  frame.first_draw_index = 0;
  frame.draw_count = draws_.size();
  frame.draw_count = draws_.size();
  frame.shader_count = 0;
  frame.constant_count = 0;
  frame.swap_count = 0;
  capture.frames.push_back(frame);
  capture.draws = draws_;
  capture.summary.total_events = draws_.size();
  return capture;
}

}  // namespace bo2::native
