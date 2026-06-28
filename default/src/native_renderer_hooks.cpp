#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ppc/function.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/types.h>

#include "../../src/native_renderer/HostDetour.h"
#include "../../src/native_renderer/NativeRenderer.h"
#include "generated/default_init.h"

namespace rex::kernel::xboxkrnl {
void VdSetSystemCommandBufferGpuIdentifierAddress_entry(mapped_void unk);
void VdSwap_entry(mapped_void buffer_ptr, mapped_void fetch_ptr,
                  mapped_void unk2, mapped_void unk3, mapped_void unk4,
                  mapped_u32 frontbuffer_ptr, mapped_u32 texture_format_ptr,
                  mapped_u32 color_space_ptr, mapped_u32 width,
                  mapped_u32 height);
} // namespace rex::kernel::xboxkrnl

namespace {

constexpr uint32_t kImpVdSetSystemCommandBufferGpuIdentifierAddress =
    0x826EAF4C;
constexpr uint32_t kImpVdSwap = 0x826EAF9C;
constexpr uint32_t kDrawAutoIndexShaderBootstrap = 0x825828D8;
constexpr uint32_t kDrawPacketCandidate = 0x82582A30;
constexpr uint32_t kDrawBatched = 0x8258A5E8;
constexpr uint32_t kShaderUploadImmediate = 0x8258CCE8;
constexpr uint32_t kShaderConstants = 0x8258CE40;
constexpr uint32_t kDrawVariable = 0x8258CF68;
constexpr uint32_t kShaderBinding = 0x82597DF8;
constexpr uint32_t kAluConstants = 0x82597F50;
constexpr uint32_t kMaterialShaderLoad = 0x82598140;
constexpr uint32_t kCommandBufferGrow = 0x8257AB00;
constexpr uint32_t kCommandBufferReserve = 0x8257AD38;

PPCFunc *original_draw_autoindex_shader_bootstrap;
PPCFunc *original_draw_packet_candidate;
PPCFunc *original_draw_batched;
PPCFunc *original_shader_upload_immediate;
PPCFunc *original_shader_constants;
PPCFunc *original_draw_variable;
PPCFunc *original_shader_binding;
PPCFunc *original_alu_constants;
PPCFunc *original_material_shader_load;
PPCFunc *original_command_buffer_grow;
PPCFunc *original_command_buffer_reserve;

void OnNativeRendererPM4Packet(
    const rex::graphics::NativeRendererPM4PacketEvent *event, void *) {
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

void OnNativeRendererDraw(const rex::graphics::NativeRendererDrawEvent *event,
                          void *) {
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
  draw.major_mode = event->major_mode;
  draw.explicit_major_mode = event->explicit_major_mode;
  draw.viz_query_condition = event->viz_query_condition;
  draw.vertex_shader_hash = event->vertex_shader_hash;
  draw.pixel_shader_hash = event->pixel_shader_hash;
  bo2::native::NativeRenderer::Instance().OnPM4Draw(draw);
}

void OnNativeRendererShader(
    const rex::graphics::NativeRendererShaderEvent *event, void *) {
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
  bo2::native::NativeRenderer::Instance().OnPM4Shader(shader);
}

template <typename ConstantEvent>
void CopyConstantPayloadIfPresent(const ConstantEvent *event,
                                  bo2::native::PM4ConstantInfo &constants) {
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
    const rex::graphics::NativeRendererConstantEvent *event, void *) {
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

void OnNativeRendererSwap(const rex::graphics::NativeRendererSwapEvent *event,
                          void *) {
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
  bo2::native::NativeRenderer::Instance().OnPM4Swap(swap);
}

void InstallCommandProcessorTraceSink(rex::Runtime *runtime) {
  auto *graphics_system =
      static_cast<rex::graphics::GraphicsSystem *>(runtime->graphics_system());
  auto *command_processor =
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

void InstallDispatcherHook(rex::runtime::FunctionDispatcher *dispatcher,
                           uint32_t address, PPCFunc *hook, PPCFunc **original,
                           const char *name) {
  *original = dispatcher->GetFunction(address);
  if (!*original) {
    REXLOG_WARN("BO2 native renderer could not find original {} at {:#010x}",
                name, address);
    return;
  }
  dispatcher->SetFunction(address, hook);
}

void InstallGeneratedDispatcherHook(
    rex::runtime::FunctionDispatcher *dispatcher, uint32_t address,
    PPCFunc *target, PPCFunc *hook, PPCFunc **original, const char *name) {
  InstallDispatcherHook(dispatcher, address, hook, original, name);
#if REX_PLATFORM_WIN32
  if (auto *trampoline =
          bo2::native::InstallGeneratedFunctionDetour(target, hook, name)) {
    *original = trampoline;
  }
#else
  (void)target;
#endif
}

void RunDrawPacketHook(std::string_view function_name,
                       uint32_t function_address, PPCContext &ctx,
                       uint8_t *base, PPCFunc *original) {
  auto &renderer = bo2::native::NativeRenderer::Instance();
  auto draw =
      renderer.OnDrawPacketCandidateBegin(function_name, function_address, ctx);
  if (original) {
    original(ctx, base);
  }
  renderer.OnDrawPacketCandidateEnd(draw);
}

void RunRenderPacketEventHook(std::string_view function_name,
                              uint32_t function_address, PPCContext &ctx,
                              uint8_t *base, PPCFunc *original) {
  auto &renderer = bo2::native::NativeRenderer::Instance();
  auto event =
      renderer.OnCommandBufferEventBegin(function_name, function_address, ctx);
  if (original) {
    original(ctx, base);
  }
  renderer.OnCommandBufferEventEnd(event, ctx);
}

REX_HOOK_RAW(
    default_native_vd_set_system_command_buffer_gpu_identifier_address) {
  bo2::native::NativeRenderer::Instance()
      .OnSystemCommandBufferGpuIdentifierAddress(ctx.r3.u32);
  rex::ppc::HostToGuestFunction<
      rex::kernel::xboxkrnl::
          VdSetSystemCommandBufferGpuIdentifierAddress_entry>(ctx, base);
}

REX_HOOK_RAW(default_native_vd_swap) {
  auto &renderer = bo2::native::NativeRenderer::Instance();
  const auto swap = renderer.OnVdSwapBegin(ctx, base);
  if (!renderer.ShouldSuppressEmulatedPresent()) {
    rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx,
                                                                       base);
    renderer.OnVdSwapEnd(swap, true);
  } else {
    renderer.OnVdSwapEnd(swap, false);
  }
}

REX_HOOK_RAW(default_native_draw_autoindex_shader_bootstrap) {
  RunDrawPacketHook("sub_825828D8", kDrawAutoIndexShaderBootstrap, ctx, base,
                    original_draw_autoindex_shader_bootstrap);
}

REX_HOOK_RAW(default_native_draw_packet_candidate) {
  RunDrawPacketHook("sub_82582A30", kDrawPacketCandidate, ctx, base,
                    original_draw_packet_candidate);
}

REX_HOOK_RAW(default_native_draw_batched) {
  RunDrawPacketHook("sub_8258A5E8", kDrawBatched, ctx, base,
                    original_draw_batched);
}

REX_HOOK_RAW(default_native_shader_upload_immediate) {
  RunRenderPacketEventHook("sub_8258CCE8", kShaderUploadImmediate, ctx, base,
                           original_shader_upload_immediate);
}

REX_HOOK_RAW(default_native_shader_constants) {
  RunRenderPacketEventHook("sub_8258CE40", kShaderConstants, ctx, base,
                           original_shader_constants);
}

REX_HOOK_RAW(default_native_draw_variable) {
  RunDrawPacketHook("sub_8258CF68", kDrawVariable, ctx, base,
                    original_draw_variable);
}

REX_HOOK_RAW(default_native_shader_binding) {
  RunRenderPacketEventHook("sub_82597DF8", kShaderBinding, ctx, base,
                           original_shader_binding);
}

REX_HOOK_RAW(default_native_alu_constants) {
  RunRenderPacketEventHook("sub_82597F50", kAluConstants, ctx, base,
                           original_alu_constants);
}

REX_HOOK_RAW(default_native_material_shader_load) {
  RunRenderPacketEventHook("sub_82598140", kMaterialShaderLoad, ctx, base,
                           original_material_shader_load);
}

REX_HOOK_RAW(default_native_command_buffer_grow) {
  RunRenderPacketEventHook("sub_8257AB00", kCommandBufferGrow, ctx, base,
                           original_command_buffer_grow);
}

REX_HOOK_RAW(default_native_command_buffer_reserve) {
  RunRenderPacketEventHook("sub_8257AD38", kCommandBufferReserve, ctx, base,
                           original_command_buffer_reserve);
}

} // namespace

namespace bo2 {

void InstallDefaultNativeRenderer(rex::Runtime *runtime) {
  auto &renderer = native::NativeRenderer::Instance();
  renderer.ConfigureForApp("default");
  if (!renderer.ShouldInstallHooks()) {
    return;
  }

  InstallCommandProcessorTraceSink(runtime);

  auto *dispatcher = runtime->function_dispatcher();

  dispatcher->SetFunction(
      kImpVdSetSystemCommandBufferGpuIdentifierAddress,
      &default_native_vd_set_system_command_buffer_gpu_identifier_address);
  dispatcher->SetFunction(kImpVdSwap, &default_native_vd_swap);
  InstallGeneratedDispatcherHook(
      dispatcher, kDrawAutoIndexShaderBootstrap, &sub_825828D8,
      &default_native_draw_autoindex_shader_bootstrap,
      &original_draw_autoindex_shader_bootstrap, "sub_825828D8");
  InstallGeneratedDispatcherHook(
      dispatcher, kDrawPacketCandidate, &sub_82582A30,
      &default_native_draw_packet_candidate, &original_draw_packet_candidate,
      "sub_82582A30");
  InstallGeneratedDispatcherHook(dispatcher, kDrawBatched, &sub_8258A5E8,
                                 &default_native_draw_batched,
                                 &original_draw_batched, "sub_8258A5E8");
  InstallGeneratedDispatcherHook(
      dispatcher, kShaderUploadImmediate, &sub_8258CCE8,
      &default_native_shader_upload_immediate,
      &original_shader_upload_immediate, "sub_8258CCE8");
  InstallGeneratedDispatcherHook(dispatcher, kShaderConstants, &sub_8258CE40,
                                 &default_native_shader_constants,
                                 &original_shader_constants, "sub_8258CE40");
  InstallGeneratedDispatcherHook(dispatcher, kDrawVariable, &sub_8258CF68,
                                 &default_native_draw_variable,
                                 &original_draw_variable, "sub_8258CF68");
  InstallGeneratedDispatcherHook(dispatcher, kShaderBinding, &sub_82597DF8,
                                 &default_native_shader_binding,
                                 &original_shader_binding, "sub_82597DF8");
  InstallGeneratedDispatcherHook(dispatcher, kAluConstants, &sub_82597F50,
                                 &default_native_alu_constants,
                                 &original_alu_constants, "sub_82597F50");
  InstallGeneratedDispatcherHook(dispatcher, kMaterialShaderLoad, &sub_82598140,
                                 &default_native_material_shader_load,
                                 &original_material_shader_load,
                                 "sub_82598140");
  InstallGeneratedDispatcherHook(dispatcher, kCommandBufferGrow, &sub_8257AB00,
                                 &default_native_command_buffer_grow,
                                 &original_command_buffer_grow, "sub_8257AB00");
  InstallGeneratedDispatcherHook(
      dispatcher, kCommandBufferReserve, &sub_8257AD38,
      &default_native_command_buffer_reserve, &original_command_buffer_reserve,
      "sub_8257AD38");

  native::InstallHostDetour(
      &__imp__VdSetSystemCommandBufferGpuIdentifierAddress,
      &default_native_vd_set_system_command_buffer_gpu_identifier_address,
      "__imp__VdSetSystemCommandBufferGpuIdentifierAddress");
  native::InstallHostDetour(&__imp__VdSwap, &default_native_vd_swap,
                            "__imp__VdSwap");
}

} // namespace bo2
