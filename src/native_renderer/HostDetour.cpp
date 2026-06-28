#include "HostDetour.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <rex/logging.h>
#include <rex/platform.h>

#if REX_PLATFORM_WIN32
#include <windows.h>
#endif

namespace bo2::native {

namespace {

constexpr size_t kAbsoluteJumpSize = 12;

#if REX_PLATFORM_WIN32
std::string FormatBytes(const uint8_t* bytes, size_t byte_count) {
  std::string result;
  result.reserve(byte_count * 3);
  constexpr char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < byte_count; ++i) {
    if (i) {
      result.push_back(' ');
    }
    result.push_back(kHex[bytes[i] >> 4]);
    result.push_back(kHex[bytes[i] & 0x0F]);
  }
  return result;
}

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

size_t GeneratedFunctionPatchSize(const uint8_t* patch, const char* name) {
  constexpr uint8_t kLegacyGeneratedPrologue[kAbsoluteJumpSize] = {
      0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0xD6,
  };
  if (std::memcmp(patch, kLegacyGeneratedPrologue, kAbsoluteJumpSize) == 0) {
    return kAbsoluteJumpSize;
  }

  // Clang/COFF debug builds emitted by this project begin generated PPC bodies
  // by reserving stack space and spilling the PPCContext/base arguments.
  if (patch[0] == 0x48 && patch[1] == 0x83 && patch[2] == 0xEC &&
      patch[4] == 0x48 && patch[5] == 0x89 && patch[6] == 0x54 &&
      patch[7] == 0x24 && patch[9] == 0x48 && patch[10] == 0x89 &&
      patch[11] == 0x4C && patch[12] == 0x24) {
    return 14;
  }

  REXLOG_ERROR(
      "BO2 native renderer unexpected generated-function prologue for {} at {}; "
      "bytes={} ; direct-call detour disabled",
      name, static_cast<void*>(const_cast<uint8_t*>(patch)), FormatBytes(patch, 32));
  return 0;
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
  auto* patch = reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(target));
  const size_t patch_size = GeneratedFunctionPatchSize(patch, name);
  if (!patch_size) {
    return nullptr;
  }
  const size_t trampoline_size = patch_size + kAbsoluteJumpSize;

  auto* trampoline = static_cast<uint8_t*>(
      VirtualAlloc(nullptr, trampoline_size, MEM_COMMIT | MEM_RESERVE,
                   PAGE_EXECUTE_READWRITE));
  if (!trampoline) {
    REXLOG_ERROR("BO2 native renderer failed to allocate trampoline for {}", name);
    return nullptr;
  }

  std::memcpy(trampoline, patch, patch_size);
  WriteAbsoluteJump(trampoline + patch_size,
                    reinterpret_cast<PPCFunc*>(patch + patch_size));

  DWORD old_protect = 0;
  if (!MakeWritable(patch, patch_size, &old_protect, name)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return nullptr;
  }

  WriteAbsoluteJump(patch, replacement);
  if (patch_size > kAbsoluteJumpSize) {
    std::memset(patch + kAbsoluteJumpSize, 0x90, patch_size - kAbsoluteJumpSize);
  }
  FlushInstructionCache(GetCurrentProcess(), patch, patch_size);

  DWORD unused_protect = 0;
  VirtualProtect(patch, patch_size, old_protect, &unused_protect);
  REXLOG_INFO(
      "BO2 native renderer installed generated-function detour for {} at {} "
      "patch_size={}",
      name, static_cast<void*>(patch), patch_size);
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
