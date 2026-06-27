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
  void OnDrawPacketCandidate(std::string_view function_name, uint32_t function_address,
                             PPCContext& ctx);
  void Shutdown();

 private:
  NativeRenderer() = default;

  bool EnsureBackend();
  VdSwapInfo CaptureVdSwap(PPCContext& ctx, uint8_t* base) const;
  CommandBufferSnapshot CaptureCommandBufferSnapshot(const VdSwapInfo& swap) const;

  RendererConfig config_;
  std::unique_ptr<RendererBackend> backend_;
  RenderResourceManager resources_;
  ShaderManager shaders_;
  TextureManager textures_;
  BufferManager buffers_;
  uint64_t vd_swap_count_ = 0;
  uint64_t draw_candidate_count_ = 0;
  uint32_t system_command_buffer_gpu_identifier_ = 0;
  bool configured_ = false;
};

}  // namespace bo2::native
