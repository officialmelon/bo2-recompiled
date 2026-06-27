#pragma once

#include <cstdint>

namespace bo2::native {

class TextureManager {
 public:
  void Reset();
  uint64_t missing_texture_count() const { return missing_texture_count_; }
  void RecordMissingTexture() { ++missing_texture_count_; }

 private:
  uint64_t missing_texture_count_ = 0;
};

}  // namespace bo2::native
