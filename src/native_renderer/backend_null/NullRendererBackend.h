#pragma once

#include <string>
#include <string_view>

#include "../NativeRenderCaptureWriter.h"
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
  void SubmitShaderRecordProbe(const ShaderRecordProbeInfo& probe) override;
  void SubmitPM4Packet(const PM4PacketInfo& packet) override;
  void SubmitPM4Draw(const PM4DrawInfo& draw) override;
  void SubmitPM4Shader(const PM4ShaderInfo& shader) override;
  void SubmitPM4Constants(const PM4ConstantInfo& constants) override;
  void SubmitPM4Swap(const PM4SwapInfo& swap) override;
  void SubmitRenderCommand(const RenderCommand& command) override;
  void EndFrame(uint64_t frame_index) override;
  std::string_view LastError() const override { return last_error_; }

 private:
  bool verbose_ = true;
  std::string app_name_;
  std::string last_error_;
  NativeRenderCaptureWriter capture_;
};

}  // namespace bo2::native
