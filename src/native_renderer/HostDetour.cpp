#include "HostDetour.h"

#include <cstdint>

#include <rex/logging.h>
#include <rex/platform.h>

#if REX_PLATFORM_WIN32
#include <windows.h>
#endif

namespace bo2::native {

bool InstallHostDetour(PPCFunc* target, PPCFunc* replacement, const char* name) {
#if REX_PLATFORM_WIN32
  constexpr size_t kPatchSize = 12;
  auto* patch = reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(target));
  DWORD old_protect = 0;
  if (!VirtualProtect(patch, kPatchSize, PAGE_EXECUTE_READWRITE, &old_protect)) {
    REXLOG_ERROR("BO2 native renderer failed to install host detour for {}", name);
    return false;
  }

  patch[0] = 0x48;
  patch[1] = 0xB8;
  *reinterpret_cast<uint64_t*>(patch + 2) = reinterpret_cast<uint64_t>(replacement);
  patch[10] = 0xFF;
  patch[11] = 0xE0;
  FlushInstructionCache(GetCurrentProcess(), patch, kPatchSize);

  DWORD unused_protect = 0;
  VirtualProtect(patch, kPatchSize, old_protect, &unused_protect);
  REXLOG_INFO("BO2 native renderer installed host detour for {}", name);
  return true;
#else
  (void)target;
  (void)replacement;
  REXLOG_WARN("BO2 native renderer host detour for {} is not implemented on this platform", name);
  return false;
#endif
}

}  // namespace bo2::native
