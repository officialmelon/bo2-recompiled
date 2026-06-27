#pragma once

#include <cstdint>

namespace bo2::native {

class ShaderManager {
 public:
  void Reset();
  uint64_t missing_shader_count() const { return missing_shader_count_; }
  void RecordMissingShader() { ++missing_shader_count_; }

 private:
  uint64_t missing_shader_count_ = 0;
};

}  // namespace bo2::native
