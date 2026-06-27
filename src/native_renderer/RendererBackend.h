#pragma once

#include <string_view>

#include "RendererTypes.h"

namespace bo2::native {

class RendererBackend {
 public:
  virtual ~RendererBackend() = default;

  virtual RendererBackendKind Kind() const = 0;
  virtual bool Initialize(const RendererConfig& config) = 0;
  virtual void Shutdown() = 0;
  virtual void BeginFrame(uint64_t frame_index) = 0;
  virtual void SubmitVdSwap(uint64_t frame_index, const VdSwapInfo& swap) = 0;
  virtual void SubmitCommandBufferSnapshot(
      uint64_t frame_index, const CommandBufferSnapshot& snapshot) = 0;
  virtual void EndFrame(uint64_t frame_index) = 0;
  virtual std::string_view LastError() const = 0;
};

}  // namespace bo2::native
