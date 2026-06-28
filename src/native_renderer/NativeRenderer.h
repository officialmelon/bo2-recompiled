#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include <rex/ppc/context.h>

#include "BufferManager.h"
#include "RenderResourceManager.h"
#include "RendererTypes.h"
#include "ShaderManager.h"
#include "TextureManager.h"

namespace bo2::native {

class RendererBackend;

class NativeRenderer {
 public:
  static NativeRenderer& Instance();

  void ConfigureForApp(std::string_view app_name);
  bool ShouldInstallHooks() const;
  bool ShouldSuppressEmulatedPresent() const;
  const RendererConfig& config() const { return config_; }

  void OnSystemCommandBufferGpuIdentifierAddress(uint32_t address);
  VdSwapInfo OnVdSwapBegin(PPCContext& ctx, uint8_t* base);
  void OnVdSwapEnd(const VdSwapInfo& swap, bool command_buffer_written);
  DrawPacketCandidateInfo OnDrawPacketCandidateBegin(std::string_view function_name,
                                                     uint32_t function_address,
                                                     PPCContext& ctx);
  void OnDrawPacketCandidateEnd(DrawPacketCandidateInfo& draw);
  CommandBufferEventInfo OnCommandBufferEventBegin(std::string_view function_name,
                                                   uint32_t function_address,
                                                   PPCContext& ctx);
  void OnCommandBufferEventEnd(CommandBufferEventInfo& event, PPCContext& ctx);
  void OnPM4Packet(PM4PacketInfo packet);
  void OnPM4Draw(PM4DrawInfo draw);
  void OnPM4Shader(PM4ShaderInfo shader);
  void OnPM4Constants(PM4ConstantInfo constants);
  void OnPM4Swap(PM4SwapInfo swap);
  void Shutdown();

 private:
  NativeRenderer() = default;

  bool EnsureBackend();
  VdSwapInfo CaptureVdSwap(PPCContext& ctx, uint8_t* base) const;
  CommandBufferSnapshot CaptureCommandBufferSnapshot(const VdSwapInfo& swap) const;
  void CaptureDrawPacketWrites(DrawPacketCandidateInfo& draw) const;

  RendererConfig config_;
  std::unique_ptr<RendererBackend> backend_;
  RenderResourceManager resources_;
  ShaderManager shaders_;
  TextureManager textures_;
  BufferManager buffers_;
  uint64_t vd_swap_count_ = 0;
  uint64_t draw_candidate_count_ = 0;
  uint64_t command_buffer_event_count_ = 0;
  uint64_t pm4_packet_count_ = 0;
  uint64_t pm4_draw_count_ = 0;
  uint64_t pm4_shader_count_ = 0;
  uint64_t pm4_constant_count_ = 0;
  uint64_t pm4_swap_count_ = 0;
  uint32_t system_command_buffer_gpu_identifier_ = 0;
  bool configured_ = false;
};

}  // namespace bo2::native
