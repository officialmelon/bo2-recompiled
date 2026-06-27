#pragma once

#include <cstdint>

namespace bo2::native {

class BufferManager {
 public:
  void Reset();
  uint64_t uploaded_buffer_count() const { return uploaded_buffer_count_; }
  void RecordUpload() { ++uploaded_buffer_count_; }

 private:
  uint64_t uploaded_buffer_count_ = 0;
};

}  // namespace bo2::native
