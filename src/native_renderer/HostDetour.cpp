#include "HostDetour.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <rex/logging.h>
#include <rex/platform.h>

#if REX_PLATFORM_WIN32
#include <windows.h>
#endif

namespace bo2::native {

namespace {

constexpr size_t kAbsoluteJumpSize = 12;

#if REX_PLATFORM_WIN32
void WriteAbsoluteJump(uint8_t* patch, PPCFunc* replacement) {
  patch[0] = 0x48;
  patch[1] = 0xB8;
  *reinterpret_cast<uint64_t*>(patch + 2) = reinterpret_cast<uint64_t>(replacement);
  patch[10] = 0xFF;
  patch[11] = 0xE0;
}

bool MakeWritable(uint8_t* patch, size_t patch_size, DWORD* old_protect,
                  const char* name) {
  if (VirtualProtect(patch, patch_size, PAGE_EXECUTE_READWRITE, old_protect)) {
    return true;
  }
  REXLOG_ERROR("BO2 native renderer failed to make {} writable", name);
  return false;
}
#endif

}  // namespace

bool InstallHostDetour(PPCFunc* target, PPCFunc* replacement, const char* name) {
#if REX_PLATFORM_WIN32
  auto* patch = reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(target));
  DWORD old_protect = 0;
  if (!MakeWritable(patch, kAbsoluteJumpSize, &old_protect, name)) {
    return false;
  }

  WriteAbsoluteJump(patch, replacement);
  FlushInstructionCache(GetCurrentProcess(), patch, kAbsoluteJumpSize);

  DWORD unused_protect = 0;
  VirtualProtect(patch, kAbsoluteJumpSize, old_protect, &unused_protect);
  REXLOG_INFO("BO2 native renderer installed host detour for {}", name);
  return true;
#else
  (void)target;
  (void)replacement;
  REXLOG_WARN("BO2 native renderer host detour for {} is not implemented on this platform", name);
  return false;
#endif
}

PPCFunc* InstallGeneratedFunctionDetour(PPCFunc* target, PPCFunc* replacement,
                                        const char* name) {
#if REX_PLATFORM_WIN32
  constexpr uint8_t kExpectedGeneratedPrologue[kAbsoluteJumpSize] = {
      0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0xD6,
  };
  constexpr size_t kTrampolineSize = kAbsoluteJumpSize + kAbsoluteJumpSize;

  auto* patch = reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(target));
  if (std::memcmp(patch, kExpectedGeneratedPrologue, kAbsoluteJumpSize) != 0) {
    REXLOG_ERROR(
        "BO2 native renderer unexpected generated-function prologue for {}; "
        "direct-call detour disabled",
        name);
    return nullptr;
  }

  auto* trampoline = static_cast<uint8_t*>(
      VirtualAlloc(nullptr, kTrampolineSize, MEM_COMMIT | MEM_RESERVE,
                   PAGE_EXECUTE_READWRITE));
  if (!trampoline) {
    REXLOG_ERROR("BO2 native renderer failed to allocate trampoline for {}", name);
    return nullptr;
  }

  std::memcpy(trampoline, patch, kAbsoluteJumpSize);
  WriteAbsoluteJump(trampoline + kAbsoluteJumpSize,
                    reinterpret_cast<PPCFunc*>(patch + kAbsoluteJumpSize));

  DWORD old_protect = 0;
  if (!MakeWritable(patch, kAbsoluteJumpSize, &old_protect, name)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return nullptr;
  }

  WriteAbsoluteJump(patch, replacement);
  FlushInstructionCache(GetCurrentProcess(), patch, kAbsoluteJumpSize);

  DWORD unused_protect = 0;
  VirtualProtect(patch, kAbsoluteJumpSize, old_protect, &unused_protect);
  REXLOG_INFO("BO2 native renderer installed generated-function detour for {}", name);
  return reinterpret_cast<PPCFunc*>(trampoline);
#else
  (void)target;
  (void)replacement;
  REXLOG_WARN(
      "BO2 native renderer generated-function detour for {} is not implemented on this "
      "platform",
      name);
  return nullptr;
#endif
}

}  // namespace bo2::native
