#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include "RenderCommand.h"
#include "RendererTypes.h"

namespace bo2::native {

class NativeRenderCaptureWriter {
 public:
  bool Initialize(const RendererConfig& config);
  void Shutdown();

  bool enabled() const { return enabled_; }
  const std::filesystem::path& path() const { return path_; }

  void WriteBeginFrame(uint64_t frame_index);
  void WriteEndFrame(uint64_t frame_index);
  void WriteVdSwap(uint64_t frame_index, const VdSwapInfo& swap);
  void WriteCommandBufferSnapshot(uint64_t frame_index,
                                  const CommandBufferSnapshot& snapshot);
  void WriteDrawPacketCandidate(const DrawPacketCandidateInfo& draw);
  void WriteShaderRecordProbe(const ShaderRecordProbeInfo& probe);
  void WritePM4Packet(const PM4PacketInfo& packet);
  void WritePM4Draw(const PM4DrawInfo& draw);
  void WritePM4Shader(const PM4ShaderInfo& shader);
  void WritePM4Constants(const PM4ConstantInfo& constants);
  void WritePM4Swap(const PM4SwapInfo& swap);
  void WriteRenderCommand(const RenderCommand& command);

 private:
  bool BeginEvent(std::string_view type);
  void EndEvent();
  void MaybeFlush();
  void WriteFieldPrefix(std::string_view name);

  void WriteStringField(std::string_view name, std::string_view value);
  void WriteBoolField(std::string_view name, bool value);
  void WriteU64Field(std::string_view name, uint64_t value);
  void WriteHex32Field(std::string_view name, uint32_t value);
  void WriteHex64Field(std::string_view name, uint64_t value);
  void WriteHexSizeField(std::string_view name, uintptr_t value);
  void FinalizeResourceIndex();
  bool WriteBinaryResource(std::string_view type, const uint8_t* data,
                           uint32_t byte_count,
                           std::string& relative_path_out);

  std::mutex mutex_;
  std::ofstream file_;
  std::ofstream resource_index_file_;
  std::ofstream resource_index_jsonl_file_;
  std::filesystem::path path_;
  std::filesystem::path resource_dir_;
  std::string app_name_;
  uint64_t event_count_ = 0;
  uint64_t event_limit_ = 0;
  uint64_t resource_count_ = 0;
  uint32_t flush_interval_ = 1024;
  bool enabled_ = false;
  bool limited_ = false;
  bool field_written_ = false;
  bool resource_index_first_ = true;
};

}  // namespace bo2::native
