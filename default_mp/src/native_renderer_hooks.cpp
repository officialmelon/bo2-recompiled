#include <rex/hook.h>
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
constexpr uint32_t kDrawPacketCandidate = 0x82117D20;

PPCFunc* original_draw_packet_candidate;

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

REX_HOOK_RAW(default_mp_native_draw_packet_candidate) {
  bo2::native::NativeRenderer::Instance().OnDrawPacketCandidate(
      "sub_82117D20", kDrawPacketCandidate, ctx);
  if (original_draw_packet_candidate) {
    original_draw_packet_candidate(ctx, base);
  } else {
    sub_82117D20(ctx, base);
  }
}

}  // namespace

namespace bo2 {

void InstallDefaultMpNativeRenderer(rex::Runtime* runtime) {
  auto& renderer = native::NativeRenderer::Instance();
  renderer.ConfigureForApp("default_mp");
  if (!renderer.ShouldInstallHooks()) {
    return;
  }

  runtime->function_dispatcher()->SetFunction(
      kImpVdSetSystemCommandBufferGpuIdentifierAddress,
      &default_mp_native_vd_set_system_command_buffer_gpu_identifier_address);
  runtime->function_dispatcher()->SetFunction(kImpVdSwap, &default_mp_native_vd_swap);
  runtime->function_dispatcher()->SetFunction(kDrawPacketCandidate,
                                               &default_mp_native_draw_packet_candidate);
  original_draw_packet_candidate = native::InstallGeneratedFunctionDetour(
      &sub_82117D20, &default_mp_native_draw_packet_candidate, "sub_82117D20");

  native::InstallHostDetour(
      &__imp__VdSetSystemCommandBufferGpuIdentifierAddress,
      &default_mp_native_vd_set_system_command_buffer_gpu_identifier_address,
      "__imp__VdSetSystemCommandBufferGpuIdentifierAddress");
  native::InstallHostDetour(&__imp__VdSwap, &default_mp_native_vd_swap, "__imp__VdSwap");
}

}  // namespace bo2
