#pragma once

#include <string>
#include <string_view>

#include "../RendererBackend.h"

namespace bo2::native {

class NullRendererBackend final : public RendererBackend {
 public:
  RendererBackendKind Kind() const override { return RendererBackendKind::NullDebug; }
  bool Initialize(const RendererConfig& config) override;
  void Shutdown() override;
  void BeginFrame(uint64_t frame_index) override;
  void SubmitVdSwap(uint64_t frame_index, const VdSwapInfo& swap) override;
  void SubmitCommandBufferSnapshot(uint64_t frame_index,
                                   const CommandBufferSnapshot& snapshot) override;
  void SubmitDrawPacketCandidate(const DrawPacketCandidateInfo& draw) override;
  void EndFrame(uint64_t frame_index) override;
  std::string_view LastError() const override { return last_error_; }

 private:
  bool verbose_ = true;
  std::string app_name_;
  std::string last_error_;
};

}  // namespace bo2::native
