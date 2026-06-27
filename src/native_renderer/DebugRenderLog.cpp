#include "DebugRenderLog.h"

namespace bo2::native {

bool ShouldLogHighFrequencyEvent(uint64_t event_index) {
  return event_index < 8 || (event_index % 120) == 0;
}

}  // namespace bo2::native
