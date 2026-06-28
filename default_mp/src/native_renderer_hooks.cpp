#include <rex/hook.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ppc/function.h>
#include <rex/runtime.h>
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

PPCFunc* original_draw_autoindex_shader_bootstrap;
PPCFunc* original_draw_packet_candidate;
PPCFunc* original_shader_upload_immediate;
PPCFunc* original_draw_batched;
PPCFunc* original_draw_variable;

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
  bo2::native::NativeRenderer::Instance().OnPM4Shader(shader);
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
  if (!renderer.ShouldSuppressEmulatedPresent()) {
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

  native::InstallHostDetour(
      &__imp__VdSetSystemCommandBufferGpuIdentifierAddress,
      &default_mp_native_vd_set_system_command_buffer_gpu_identifier_address,
      "__imp__VdSetSystemCommandBufferGpuIdentifierAddress");
  native::InstallHostDetour(&__imp__VdSwap, &default_mp_native_vd_swap, "__imp__VdSwap");
}

}  // namespace bo2
