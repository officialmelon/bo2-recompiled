#pragma once

#include <cstdint>

namespace bo2::native {

class RenderResourceManager {
 public:
  void Reset();
  uint64_t generation() const { return generation_; }

 private:
  uint64_t generation_ = 0;
};

}  // namespace bo2::native
