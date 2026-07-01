#include <algorithm>
#include <cstring>
#include <iterator>

#include <rex/hook.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ppc/function.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/function_dispatcher.h>
#include <rex/types.h>

#include "generated/default_mp_init.h"
#include "../../src/native_renderer/HostDetour.h"
#include "../../src/native_renderer/NativeRenderer.h"

namespace rex::kernel::xboxkrnl {
void VdSetSystemCommandBufferGpuIdentifierAddress_entry(mapped_void unk);
void VdSwap_entry(mapped_void buffer_ptr, mapped_void fetch_ptr, mapped_void unk2,
                  mapped_void unk3, mapped_void unk4, mapped_u32 frontbuffer_ptr,
                  mapped_u32 texture_format_ptr, mapped_u32 color_space_ptr,
                  mapped_u32 width, mapped_u32 height);
}  // namespace rex::kernel::xboxkrnl

namespace {

constexpr uint32_t kImpVdSetSystemCommandBufferGpuIdentifierAddress = 0x827FBA14;
constexpr uint32_t kImpVdSwap = 0x827FBB94;
constexpr uint32_t kDrawAutoIndexShaderBootstrap = 0x82117BC8;
constexpr uint32_t kDrawPacketCandidate = 0x82117D20;
constexpr uint32_t kShaderUploadImmediate = 0x8212D478;
constexpr uint32_t kDrawBatched = 0x8212E280;
constexpr uint32_t kDrawVariable = 0x8212EB40;
constexpr uint32_t kMaxProjectTexturePayloadBytes = 8 * 1024 * 1024;
constexpr uint32_t kMaxProjectVertexPayloadBytes = 8 * 1024 * 1024;

PPCFunc* original_draw_autoindex_shader_bootstrap;
PPCFunc* original_draw_packet_candidate;
PPCFunc* original_shader_upload_immediate;
PPCFunc* original_draw_batched;
PPCFunc* original_draw_variable;

uint32_t AlignUpU32(uint32_t value, uint32_t alignment) {
  return alignment == 0 ? value
                        : ((value + alignment - 1) / alignment) * alignment;
}

uint32_t XenosTiledOffset2D(uint32_t x, uint32_t y, uint32_t pitch,
                            uint32_t bytes_per_block_log2) {
  pitch = AlignUpU32(pitch, 32);
  const uint32_t macro =
      ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bytes_per_block_log2 + 7);
  const uint32_t micro =
      ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  const uint32_t offset =
      macro + ((micro & ~0xFu) << 1) + (micro & 0xFu) + ((y & 1) << 4);
  return ((offset & ~0x1FFu) << 3) + ((y & 16) << 7) +
         ((offset & 0x1C0u) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3Fu);
}

bool TextureFormatFootprint(const bo2::native::TextureFetchInfo& fetch,
                            uint32_t& footprint) {
  footprint = 0;
  if (fetch.width == 0 || fetch.height == 0) {
    return false;
  }

  uint32_t bytes_per_texel = 0;
  uint32_t bytes_per_block_log2 = 0;
  switch (fetch.format) {
    case 2:
      bytes_per_texel = 1;
      bytes_per_block_log2 = 0;
      break;
    case 6:
      bytes_per_texel = 4;
      bytes_per_block_log2 = 2;
      break;
    default:
      return false;
  }

  const uint32_t pitch_texels =
      fetch.pitch != 0 ? fetch.pitch << 5 : fetch.width;
  const uint64_t required =
      fetch.tiled
          ? uint64_t(XenosTiledOffset2D(fetch.width - 1, fetch.height - 1,
                                        pitch_texels, bytes_per_block_log2)) +
                bytes_per_texel
          : (uint64_t(pitch_texels) * (fetch.height - 1) + fetch.width) *
                bytes_per_texel;
  if (required == 0 || required > kMaxProjectTexturePayloadBytes ||
      required > UINT32_MAX) {
    return false;
  }
  footprint = static_cast<uint32_t>(required);
  return true;
}

void RecaptureTexturePayloadFromGuest(bo2::native::TextureFetchInfo& target) {
  uint32_t footprint = 0;
  if (!target.payload_truncated ||
      !TextureFormatFootprint(target, footprint) ||
      target.base_address_bytes == 0 ||
      target.payload_bytes.size() >= footprint) {
    return;
  }

  const uint8_t* source =
      REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(
          target.base_address_bytes);
  if (!source) {
    return;
  }
  target.payload_bytes.resize(footprint);
  std::memcpy(target.payload_bytes.data(), source, footprint);
  target.payload_byte_count = footprint;
  target.payload_truncated = false;
  target.payload_missing = false;
}

void RecaptureVertexPayloadFromGuest(bo2::native::VertexFetchInfo& target) {
  const uint64_t byte_count = uint64_t(target.size) << 2;
  const uint64_t byte_address = uint64_t(target.address) << 2;
  if (!target.payload_truncated || byte_count == 0 ||
      byte_count > kMaxProjectVertexPayloadBytes || byte_address == 0 ||
      target.payload_bytes.size() >= byte_count) {
    return;
  }

  const uint8_t* source =
      REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(
          static_cast<uint32_t>(byte_address));
  if (!source) {
    return;
  }
  target.payload_bytes.resize(static_cast<std::size_t>(byte_count));
  std::memcpy(target.payload_bytes.data(), source,
              static_cast<std::size_t>(byte_count));
  target.payload_byte_count = static_cast<uint32_t>(byte_count);
  target.payload_truncated = false;
  target.payload_missing = false;
}

void OnNativeRendererPM4Packet(
    const rex::graphics::NativeRendererPM4PacketEvent* event, void*) {
  if (!event) {
    return;
  }
  bo2::native::PM4PacketInfo packet{};
  packet.opcode = event->opcode;
  packet.packet = event->packet;
  packet.payload_dword_count = event->payload_dword_count;
  packet.packet_ptr = event->packet_ptr;
  packet.buffer_ptr = event->buffer_ptr;
  packet.packet_offset = event->packet_offset;
  packet.first_dword_count = event->first_dword_count;
  for (uint32_t i = 0; i < packet.first_dwords.size(); ++i) {
    packet.first_dwords[i] = event->first_dwords[i];
  }
  bo2::native::NativeRenderer::Instance().OnPM4Packet(packet);
}

template <typename DrawEvent>
void CopyDrawIndexPayloadIfPresent(const DrawEvent* event,
                                   bo2::native::PM4DrawInfo& draw) {
  if constexpr (requires {
                  event->index_payload_byte_count;
                  event->index_payload_truncated;
                  event->index_payload_missing;
                  event->index_bytes[0];
                }) {
    draw.index_payload_byte_count = event->index_payload_byte_count;
    draw.index_payload_truncated = event->index_payload_truncated;
    draw.index_payload_missing = event->index_payload_missing;
    const uint32_t copy_count = std::min<uint32_t>(
        draw.index_payload_byte_count, draw.index_bytes.size());
    for (uint32_t i = 0; i < copy_count; ++i) {
      draw.index_bytes[i] = event->index_bytes[i];
    }
  } else {
    draw.index_payload_missing = true;
  }
}

template <typename DrawEvent>
void CopyDrawVertexFetchesIfPresent(const DrawEvent* event,
                                    bo2::native::PM4DrawInfo& draw) {
  if constexpr (requires {
                  event->vertex_fetch_count;
                  event->vertex_fetch_truncated;
                  event->vertex_fetches[0].fetch_constant;
                  event->vertex_fetches[0].attributes[0].data_format;
                  event->vertex_fetches[0].payload_bytes[0];
                }) {
    draw.vertex_fetch_count = std::min<uint32_t>(
        event->vertex_fetch_count, draw.vertex_fetches.size());
    draw.vertex_fetch_truncated = event->vertex_fetch_truncated ||
                                  event->vertex_fetch_count >
                                      draw.vertex_fetches.size();
    for (uint32_t i = 0; i < draw.vertex_fetch_count; ++i) {
      const auto& source = event->vertex_fetches[i];
      auto& target = draw.vertex_fetches[i];
      target.fetch_constant = source.fetch_constant;
      target.dword_0 = source.dword_0;
      target.dword_1 = source.dword_1;
      target.type = source.type;
      target.address = source.address;
      target.size = source.size;
      target.endian = source.endian;
      target.stride_words = source.stride_words;
      target.attribute_count = source.attribute_count;
      target.captured_attribute_count = std::min<uint32_t>(
          source.captured_attribute_count, target.attributes.size());
      for (uint32_t j = 0; j < target.captured_attribute_count; ++j) {
        const auto& source_attr = source.attributes[j];
        auto& target_attr = target.attributes[j];
        target_attr.data_format = source_attr.data_format;
        target_attr.offset = source_attr.offset;
        target_attr.stride = source_attr.stride;
        target_attr.exp_adjust = source_attr.exp_adjust;
        target_attr.prefetch_count = source_attr.prefetch_count;
        target_attr.signed_rf_mode = source_attr.signed_rf_mode;
        target_attr.is_index_rounded = source_attr.is_index_rounded;
        target_attr.is_signed = source_attr.is_signed;
        target_attr.is_integer = source_attr.is_integer;
      }
      target.payload_byte_count = std::min<uint32_t>(
          source.payload_byte_count, std::size(source.payload_bytes));
      target.payload_bytes.resize(target.payload_byte_count);
      for (uint32_t j = 0; j < target.payload_byte_count; ++j) {
        target.payload_bytes[j] = source.payload_bytes[j];
      }
      target.payload_truncated = source.payload_truncated ||
                                 source.payload_byte_count >
                                     std::size(source.payload_bytes);
      target.payload_missing = source.payload_missing;
      RecaptureVertexPayloadFromGuest(target);
    }
  } else {
    draw.vertex_fetch_count = 0;
    draw.vertex_fetch_truncated = false;
  }
}

template <typename DrawEvent>
void CopyDrawTextureFetchesIfPresent(const DrawEvent* event,
                                     bo2::native::PM4DrawInfo& draw) {
  if constexpr (requires {
                  event->texture_fetch_count;
                  event->texture_fetch_truncated;
                  event->texture_fetches[0].fetch_constant;
                  event->texture_fetches[0].payload_bytes[0];
                }) {
    draw.texture_fetch_count = std::min<uint32_t>(
        event->texture_fetch_count, draw.texture_fetches.size());
    draw.texture_fetch_truncated = event->texture_fetch_truncated ||
                                   event->texture_fetch_count >
                                       draw.texture_fetches.size();
    for (uint32_t i = 0; i < draw.texture_fetch_count; ++i) {
      const auto& source = event->texture_fetches[i];
      auto& target = draw.texture_fetches[i];
      target.shader_type = source.shader_type;
      target.binding_index = source.binding_index;
      target.fetch_constant = source.fetch_constant;
      for (uint32_t j = 0; j < target.dwords.size(); ++j) {
        target.dwords[j] = source.dwords[j];
      }
      target.type = source.type;
      target.base_address = source.base_address;
      target.base_address_bytes = source.base_address_bytes;
      target.mip_address = source.mip_address;
      target.mip_address_bytes = source.mip_address_bytes;
      target.pitch = source.pitch;
      target.tiled = source.tiled;
      target.format = source.format;
      target.endian = source.endian;
      target.request_size = source.request_size;
      target.stacked = source.stacked;
      target.width = source.width;
      target.height = source.height;
      target.depth_or_stack = source.depth_or_stack;
      target.num_format = source.num_format;
      target.swizzle = source.swizzle;
      target.exp_adjust = source.exp_adjust;
      target.clamp_x = source.clamp_x;
      target.clamp_y = source.clamp_y;
      target.clamp_z = source.clamp_z;
      target.mag_filter = source.mag_filter;
      target.min_filter = source.min_filter;
      target.mip_filter = source.mip_filter;
      target.aniso_filter = source.aniso_filter;
      target.arbitrary_filter = source.arbitrary_filter;
      target.border_size = source.border_size;
      target.vol_mag_filter = source.vol_mag_filter;
      target.vol_min_filter = source.vol_min_filter;
      target.mip_min_level = source.mip_min_level;
      target.mip_max_level = source.mip_max_level;
      target.lod_bias = source.lod_bias;
      target.grad_exp_adjust_h = source.grad_exp_adjust_h;
      target.grad_exp_adjust_v = source.grad_exp_adjust_v;
      target.border_color = source.border_color;
      target.force_bc_w_to_max = source.force_bc_w_to_max;
      target.tri_clamp = source.tri_clamp;
      target.aniso_bias = source.aniso_bias;
      target.dimension = source.dimension;
      target.packed_mips = source.packed_mips;
      target.payload_byte_count = std::min<uint32_t>(
          source.payload_byte_count, std::size(source.payload_bytes));
      target.payload_bytes.resize(target.payload_byte_count);
      for (uint32_t j = 0; j < target.payload_byte_count; ++j) {
        target.payload_bytes[j] = source.payload_bytes[j];
      }
      target.payload_truncated = source.payload_truncated ||
                                 source.payload_byte_count >
                                     std::size(source.payload_bytes);
      target.payload_missing = source.payload_missing;
      RecaptureTexturePayloadFromGuest(target);
    }
  } else {
    draw.texture_fetch_count = 0;
    draw.texture_fetch_truncated = false;
  }
}

template <typename DrawEvent>
void CopyDrawRenderStateIfPresent(const DrawEvent* event,
                                  bo2::native::PM4DrawInfo& draw) {
  if constexpr (requires {
                  event->render_state.rb_surface_info;
                  event->render_state.viewport_registers[0];
                  event->render_state.rb_color_info[0];
                }) {
    const auto& source = event->render_state;
    auto& target = draw.render_state;
    target.rb_modecontrol = source.rb_modecontrol;
    target.rb_surface_info = source.rb_surface_info;
    target.rb_colorcontrol = source.rb_colorcontrol;
    target.rb_color_mask = source.rb_color_mask;
    target.rb_depthcontrol = source.rb_depthcontrol;
    target.rb_stencilrefmask = source.rb_stencilrefmask;
    target.rb_stencilrefmask_bf = source.rb_stencilrefmask_bf;
    target.rb_depth_info = source.rb_depth_info;
    target.rb_alpha_ref = source.rb_alpha_ref;
    target.pa_sc_screen_scissor_tl = source.pa_sc_screen_scissor_tl;
    target.pa_sc_screen_scissor_br = source.pa_sc_screen_scissor_br;
    target.pa_sc_window_offset = source.pa_sc_window_offset;
    target.pa_sc_window_scissor_tl = source.pa_sc_window_scissor_tl;
    target.pa_sc_window_scissor_br = source.pa_sc_window_scissor_br;
    target.pa_cl_clip_cntl = source.pa_cl_clip_cntl;
    target.pa_cl_vte_cntl = source.pa_cl_vte_cntl;
    target.pa_su_sc_mode_cntl = source.pa_su_sc_mode_cntl;
    target.pa_su_vtx_cntl = source.pa_su_vtx_cntl;
    target.sq_program_cntl = source.sq_program_cntl;
    target.sq_context_misc = source.sq_context_misc;
    for (uint32_t i = 0; i < target.viewport_registers.size(); ++i) {
      target.viewport_registers[i] = source.viewport_registers[i];
    }
    for (uint32_t i = 0; i < target.rb_color_info.size(); ++i) {
      target.rb_color_info[i] = source.rb_color_info[i];
      target.rb_blendcontrol[i] = source.rb_blendcontrol[i];
      target.color_base[i] = source.color_base[i];
      target.color_format[i] = source.color_format[i];
      target.color_exp_bias[i] = source.color_exp_bias[i];
    }
    if constexpr (requires {
                    source.color_payload_requested_byte_count[0];
                    source.color_payload_offset_bytes[0];
                    source.color_payload_byte_count[0];
                    source.color_payload_bytes[0][0];
                    source.color_payload_truncated[0];
                    source.color_payload_missing[0];
                    source.depth_payload_requested_byte_count;
                    source.depth_payload_offset_bytes;
                    source.depth_payload_byte_count;
                    source.depth_payload_bytes[0];
                  }) {
      for (uint32_t i = 0; i < target.color_payload_bytes.size(); ++i) {
        target.color_payload_requested_byte_count[i] =
            source.color_payload_requested_byte_count[i];
        target.color_payload_offset_bytes[i] =
            source.color_payload_offset_bytes[i];
        target.color_payload_byte_count[i] = source.color_payload_byte_count[i];
        target.color_payload_truncated[i] = source.color_payload_truncated[i];
        target.color_payload_missing[i] = source.color_payload_missing[i];
        const uint32_t payload_count = std::min<uint32_t>(
            source.color_payload_byte_count[i],
            std::size(source.color_payload_bytes[i]));
        if (payload_count != 0) {
          target.color_payload_bytes[i].assign(
              source.color_payload_bytes[i],
              source.color_payload_bytes[i] + payload_count);
        }
      }
      target.depth_payload_requested_byte_count =
          source.depth_payload_requested_byte_count;
      target.depth_payload_offset_bytes = source.depth_payload_offset_bytes;
      target.depth_payload_byte_count = source.depth_payload_byte_count;
      target.depth_payload_truncated = source.depth_payload_truncated;
      target.depth_payload_missing = source.depth_payload_missing;
      const uint32_t depth_payload_count = std::min<uint32_t>(
          source.depth_payload_byte_count, std::size(source.depth_payload_bytes));
      if (depth_payload_count != 0) {
        target.depth_payload_bytes.assign(
            source.depth_payload_bytes,
            source.depth_payload_bytes + depth_payload_count);
      }
    }
    target.surface_pitch = source.surface_pitch;
    target.msaa_samples = source.msaa_samples;
    target.depth_base = source.depth_base;
    target.depth_format = source.depth_format;
    target.depth_test_enable = source.depth_test_enable;
    target.depth_write_enable = source.depth_write_enable;
    target.stencil_enable = source.stencil_enable;
    target.depth_func = source.depth_func;
    target.cull_mode = source.cull_mode;
    target.fill_mode = source.fill_mode;
    target.front_face = source.front_face;
  }
}

void OnNativeRendererDraw(const rex::graphics::NativeRendererDrawEvent* event,
                          void*) {
  if (!event) {
    return;
  }
  bo2::native::PM4DrawInfo draw{};
  draw.opcode_name = event->opcode_name ? event->opcode_name : "";
  draw.opcode = event->opcode;
  draw.packet = event->packet;
  draw.packet_ptr = event->packet_ptr;
  draw.buffer_ptr = event->buffer_ptr;
  draw.packet_offset = event->packet_offset;
  draw.index_count = event->index_count;
  draw.primitive_type = event->primitive_type;
  draw.source_select = event->source_select;
  draw.indexed = event->indexed;
  draw.index_base = event->index_base;
  draw.index_length = event->index_length;
  draw.index_buffer_count = event->index_buffer_count;
  draw.index_format = event->index_format;
  draw.index_endianness = event->index_endianness;
  CopyDrawIndexPayloadIfPresent(event, draw);
  CopyDrawVertexFetchesIfPresent(event, draw);
  CopyDrawTextureFetchesIfPresent(event, draw);
  CopyDrawRenderStateIfPresent(event, draw);
  draw.major_mode = event->major_mode;
  draw.explicit_major_mode = event->explicit_major_mode;
  draw.viz_query_condition = event->viz_query_condition;
  draw.vertex_shader_hash = event->vertex_shader_hash;
  draw.pixel_shader_hash = event->pixel_shader_hash;
  bo2::native::NativeRenderer::Instance().OnPM4Draw(draw);
}

void OnNativeRendererShader(const rex::graphics::NativeRendererShaderEvent* event,
                            void*) {
  if (!event) {
    return;
  }
  bo2::native::PM4ShaderInfo shader{};
  shader.opcode = event->opcode;
  shader.packet = event->packet;
  shader.packet_ptr = event->packet_ptr;
  shader.buffer_ptr = event->buffer_ptr;
  shader.packet_offset = event->packet_offset;
  shader.shader_type = event->shader_type;
  shader.embedded = event->embedded;
  shader.guest_address = event->guest_address;
  shader.host_address = event->host_address;
  shader.dword_count = event->dword_count;
  shader.shader_hash = event->shader_hash;
  if constexpr (requires {
                  event->payload_dword_count;
                  event->payload_truncated;
                  event->payload_missing;
                  event->payload_dwords[0];
                }) {
    shader.payload_dword_count =
        std::min<uint32_t>(event->payload_dword_count,
                           shader.payload_dwords.size());
    for (uint32_t i = 0; i < shader.payload_dword_count; ++i) {
      shader.payload_dwords[i] = event->payload_dwords[i];
    }
    shader.payload_truncated = event->payload_truncated ||
                               event->payload_dword_count >
                                   shader.payload_dwords.size();
    shader.payload_missing = event->payload_missing;
  }
  bo2::native::NativeRenderer::Instance().OnPM4Shader(shader);
}

template <typename ConstantEvent>
void CopyConstantPayloadIfPresent(const ConstantEvent* event,
                                  bo2::native::PM4ConstantInfo& constants) {
  if constexpr (requires {
                  event->payload_dword_count;
                  event->payload_truncated;
                  event->payload_missing;
                  event->payload_dwords[0];
                }) {
    constants.payload_dword_count = event->payload_dword_count;
    constants.payload_truncated = event->payload_truncated;
    constants.payload_missing = event->payload_missing;
    for (uint32_t i = 0; i < constants.payload_dwords.size(); ++i) {
      constants.payload_dwords[i] = event->payload_dwords[i];
    }
  } else {
    constants.payload_missing = true;
  }
}

void OnNativeRendererConstants(
    const rex::graphics::NativeRendererConstantEvent* event, void*) {
  if (!event) {
    return;
  }
  bo2::native::PM4ConstantInfo constants{};
  constants.opcode = event->opcode;
  constants.packet = event->packet;
  constants.packet_ptr = event->packet_ptr;
  constants.buffer_ptr = event->buffer_ptr;
  constants.packet_offset = event->packet_offset;
  constants.address = event->address;
  constants.offset_type = event->offset_type;
  constants.type = event->type;
  constants.index = event->index;
  constants.dword_count = event->dword_count;
  CopyConstantPayloadIfPresent(event, constants);
  bo2::native::NativeRenderer::Instance().OnPM4Constants(constants);
}

void OnNativeRendererSwap(const rex::graphics::NativeRendererSwapEvent* event,
                          void*) {
  if (!event) {
    return;
  }
  bo2::native::PM4SwapInfo swap{};
  swap.opcode = event->opcode;
  swap.packet = event->packet;
  swap.packet_ptr = event->packet_ptr;
  swap.buffer_ptr = event->buffer_ptr;
  swap.packet_offset = event->packet_offset;
  swap.frontbuffer_ptr = event->frontbuffer_ptr;
  swap.width = event->width;
  swap.height = event->height;
  swap.frame_counter = event->frame_counter;
  swap.frontbuffer_payload_requested_byte_count =
      event->frontbuffer_payload_requested_byte_count;
  swap.frontbuffer_payload_byte_count = event->frontbuffer_payload_byte_count;
  swap.frontbuffer_payload_truncated = event->frontbuffer_payload_truncated;
  swap.frontbuffer_payload_missing = event->frontbuffer_payload_missing;
  if (event->frontbuffer_bytes &&
      event->frontbuffer_payload_byte_count != 0) {
    swap.frontbuffer_bytes.assign(
        event->frontbuffer_bytes,
        event->frontbuffer_bytes + event->frontbuffer_payload_byte_count);
  }
  if constexpr (requires {
                  event->frontbuffer_fetch_valid;
                  event->frontbuffer_fetch.fetch_constant;
                  event->frontbuffer_fetch.dwords[0];
                }) {
    swap.frontbuffer_fetch_valid = event->frontbuffer_fetch_valid;
    if (swap.frontbuffer_fetch_valid) {
      const auto& source = event->frontbuffer_fetch;
      auto& target = swap.frontbuffer_fetch;
      target.shader_type = source.shader_type;
      target.binding_index = source.binding_index;
      target.fetch_constant = source.fetch_constant;
      for (uint32_t j = 0; j < target.dwords.size(); ++j) {
        target.dwords[j] = source.dwords[j];
      }
      target.type = source.type;
      target.base_address = source.base_address;
      target.base_address_bytes = source.base_address_bytes;
      target.mip_address = source.mip_address;
      target.mip_address_bytes = source.mip_address_bytes;
      target.pitch = source.pitch;
      target.tiled = source.tiled;
      target.format = source.format;
      target.endian = source.endian;
      target.request_size = source.request_size;
      target.stacked = source.stacked;
      target.width = source.width;
      target.height = source.height;
      target.depth_or_stack = source.depth_or_stack;
      target.num_format = source.num_format;
      target.swizzle = source.swizzle;
      target.exp_adjust = source.exp_adjust;
      target.clamp_x = source.clamp_x;
      target.clamp_y = source.clamp_y;
      target.clamp_z = source.clamp_z;
      target.mag_filter = source.mag_filter;
      target.min_filter = source.min_filter;
      target.mip_filter = source.mip_filter;
      target.aniso_filter = source.aniso_filter;
      target.arbitrary_filter = source.arbitrary_filter;
      target.border_size = source.border_size;
      target.vol_mag_filter = source.vol_mag_filter;
      target.vol_min_filter = source.vol_min_filter;
      target.mip_min_level = source.mip_min_level;
      target.mip_max_level = source.mip_max_level;
      target.lod_bias = source.lod_bias;
      target.grad_exp_adjust_h = source.grad_exp_adjust_h;
      target.grad_exp_adjust_v = source.grad_exp_adjust_v;
      target.border_color = source.border_color;
      target.force_bc_w_to_max = source.force_bc_w_to_max;
      target.tri_clamp = source.tri_clamp;
      target.aniso_bias = source.aniso_bias;
      target.dimension = source.dimension;
      target.packed_mips = source.packed_mips;
      target.payload_byte_count = std::min<uint32_t>(
          source.payload_byte_count, std::size(source.payload_bytes));
      target.payload_bytes.resize(target.payload_byte_count);
      for (uint32_t j = 0; j < target.payload_byte_count; ++j) {
        target.payload_bytes[j] = source.payload_bytes[j];
      }
      target.payload_truncated = source.payload_truncated ||
                                 source.payload_byte_count >
                                     std::size(source.payload_bytes);
      target.payload_missing = source.payload_missing;
      RecaptureTexturePayloadFromGuest(target);
    }
  }
  bo2::native::NativeRenderer::Instance().OnPM4Swap(swap);
}

void InstallCommandProcessorTraceSink(rex::Runtime* runtime) {
  auto* graphics_system =
      static_cast<rex::graphics::GraphicsSystem*>(runtime->graphics_system());
  auto* command_processor =
      graphics_system ? graphics_system->command_processor() : nullptr;
  if (!command_processor) {
    REXLOG_WARN("BO2 native renderer could not register ReXGlue CP trace sink");
    return;
  }

  rex::graphics::NativeRendererTraceCallbacks callbacks{};
  callbacks.on_packet = &OnNativeRendererPM4Packet;
  callbacks.on_draw = &OnNativeRendererDraw;
  callbacks.on_shader = &OnNativeRendererShader;
  callbacks.on_constants = &OnNativeRendererConstants;
  callbacks.on_swap = &OnNativeRendererSwap;
  command_processor->SetNativeRendererTraceCallbacks(callbacks);
  REXLOG_INFO("BO2 native renderer registered ReXGlue CP trace sink");
}

void InstallDispatcherHook(rex::runtime::FunctionDispatcher* dispatcher,
                           uint32_t address, PPCFunc* hook, PPCFunc** original,
                           const char* name) {
  *original = dispatcher->GetFunction(address);
  if (!*original) {
    REXLOG_WARN("BO2 native renderer could not find original {} at {:#010x}", name,
                address);
    return;
  }
  dispatcher->SetFunction(address, hook);
}

void InstallGeneratedDispatcherHook(rex::runtime::FunctionDispatcher* dispatcher,
                                    uint32_t address, PPCFunc* target, PPCFunc* hook,
                                    PPCFunc** original, const char* name) {
  InstallDispatcherHook(dispatcher, address, hook, original, name);
#if REX_PLATFORM_WIN32
  if (auto* trampoline =
          bo2::native::InstallGeneratedFunctionDetour(target, hook, name)) {
    *original = trampoline;
  }
#else
  (void)target;
#endif
}

void RunDrawPacketHook(std::string_view function_name, uint32_t function_address,
                       PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  auto& renderer = bo2::native::NativeRenderer::Instance();
  auto draw =
      renderer.OnDrawPacketCandidateBegin(function_name, function_address, ctx);
  if (original) {
    original(ctx, base);
  }
  renderer.OnDrawPacketCandidateEnd(draw);
}

void RunRenderPacketEventHook(std::string_view function_name,
                              uint32_t function_address, PPCContext& ctx,
                              uint8_t* base, PPCFunc* original) {
  auto& renderer = bo2::native::NativeRenderer::Instance();
  auto event =
      renderer.OnCommandBufferEventBegin(function_name, function_address, ctx);
  if (original) {
    original(ctx, base);
  }
  renderer.OnCommandBufferEventEnd(event, ctx);
}

REX_HOOK_RAW(default_mp_native_vd_set_system_command_buffer_gpu_identifier_address) {
  bo2::native::NativeRenderer::Instance().OnSystemCommandBufferGpuIdentifierAddress(
      ctx.r3.u32);
  rex::ppc::HostToGuestFunction<
      rex::kernel::xboxkrnl::VdSetSystemCommandBufferGpuIdentifierAddress_entry>(ctx,
                                                                                 base);
}

REX_HOOK_RAW(default_mp_native_vd_swap) {
  auto& renderer = bo2::native::NativeRenderer::Instance();
  const auto swap = renderer.OnVdSwapBegin(ctx, base);
  if (!renderer.ShouldSuppressEmulatedPresent() &&
      renderer.CanForwardVdSwap(swap)) {
    rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx, base);
    renderer.OnVdSwapEnd(swap, true);
  } else {
    renderer.OnVdSwapEnd(swap, false);
  }
}

REX_HOOK_RAW(default_mp_native_draw_autoindex_shader_bootstrap) {
  RunDrawPacketHook("sub_82117BC8", kDrawAutoIndexShaderBootstrap, ctx, base,
                    original_draw_autoindex_shader_bootstrap);
}

REX_HOOK_RAW(default_mp_native_draw_packet_candidate) {
  RunDrawPacketHook("sub_82117D20", kDrawPacketCandidate, ctx, base,
                    original_draw_packet_candidate);
}

REX_HOOK_RAW(default_mp_native_shader_upload_immediate) {
  RunRenderPacketEventHook("sub_8212D478", kShaderUploadImmediate, ctx, base,
                           original_shader_upload_immediate);
}

REX_HOOK_RAW(default_mp_native_draw_batched) {
  RunDrawPacketHook("sub_8212E280", kDrawBatched, ctx, base,
                    original_draw_batched);
}

REX_HOOK_RAW(default_mp_native_draw_variable) {
  RunDrawPacketHook("sub_8212EB40", kDrawVariable, ctx, base,
                    original_draw_variable);
}

}  // namespace

namespace bo2 {

void InstallDefaultMpNativeRenderer(rex::Runtime* runtime) {
  auto& renderer = native::NativeRenderer::Instance();
  renderer.ConfigureForApp("default_mp");
  if (!renderer.ShouldInstallHooks()) {
    return;
  }

  InstallCommandProcessorTraceSink(runtime);

  auto* dispatcher = runtime->function_dispatcher();

  dispatcher->SetFunction(
      kImpVdSetSystemCommandBufferGpuIdentifierAddress,
      &default_mp_native_vd_set_system_command_buffer_gpu_identifier_address);
  dispatcher->SetFunction(kImpVdSwap, &default_mp_native_vd_swap);
  InstallGeneratedDispatcherHook(
      dispatcher, kDrawAutoIndexShaderBootstrap, &sub_82117BC8,
      &default_mp_native_draw_autoindex_shader_bootstrap,
      &original_draw_autoindex_shader_bootstrap, "sub_82117BC8");
  InstallGeneratedDispatcherHook(dispatcher, kDrawPacketCandidate, &sub_82117D20,
                                 &default_mp_native_draw_packet_candidate,
                                 &original_draw_packet_candidate,
                                 "sub_82117D20");
  InstallGeneratedDispatcherHook(dispatcher, kShaderUploadImmediate, &sub_8212D478,
                                 &default_mp_native_shader_upload_immediate,
                                 &original_shader_upload_immediate,
                                 "sub_8212D478");
  InstallGeneratedDispatcherHook(dispatcher, kDrawBatched, &sub_8212E280,
                                 &default_mp_native_draw_batched,
                                 &original_draw_batched, "sub_8212E280");
  InstallGeneratedDispatcherHook(dispatcher, kDrawVariable, &sub_8212EB40,
                                 &default_mp_native_draw_variable,
                                 &original_draw_variable, "sub_8212EB40");

  native::InstallImportThunkDetour(
      &__imp__VdSetSystemCommandBufferGpuIdentifierAddress,
      &default_mp_native_vd_set_system_command_buffer_gpu_identifier_address,
      "__imp__VdSetSystemCommandBufferGpuIdentifierAddress");
  native::InstallImportThunkDetour(&__imp__VdSwap, &default_mp_native_vd_swap,
                                   "__imp__VdSwap");
}

}  // namespace bo2
