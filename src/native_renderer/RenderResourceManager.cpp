#include "RenderResourceManager.h"

namespace bo2::native {

void RenderResourceManager::Reset() {
  ++generation_;
}

}  // namespace bo2::native
