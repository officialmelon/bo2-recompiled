#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../RendererTypes.h"
#include "../replay/NativeRenderReplay.h"

namespace bo2::native {

class D3D12LiveFrameBuilder {
 public:
  void BeginFrame(uint64_t frame_index);
  void AbsorbPending(D3D12LiveFrameBuilder& pending, uint64_t frame_index);
  void BindShader(const PM4ShaderInfo &shader, uint64_t seq);
  void BindConstants(const PM4ConstantInfo &constants, uint64_t seq);
  void AddDraw(const PM4DrawInfo &draw, uint64_t seq);
  replay::ReplayCapture BuildCapture(uint64_t frame_index) const;
  std::size_t draw_count() const { return draws_.size(); }

 private:
  uint64_t frame_index_ = 0;
  replay::BoundShaderState vertex_shader_;
  replay::BoundShaderState pixel_shader_;
  uint64_t constants_seen_total_ = 0;
  uint64_t constants_seen_in_frame_ = 0;
  uint64_t last_constant_seq_ = 0;
  std::vector<replay::PM4ConstantRecord> recent_constants_;
  std::map<std::pair<uint32_t, uint32_t>, replay::PM4ConstantRecord>
      bound_constants_;
  std::vector<replay::ReplayDrawState> draws_;
};

}  // namespace bo2::native
