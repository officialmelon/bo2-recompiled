#pragma once

#if defined(_WIN32)
#include <windows.h>
#endif

namespace bo2 {

inline bool EnableProcessDpiAwareness() {
#if !defined(_WIN32)
  return false;
#else
  static const bool enabled = [] {
    using SetProcessDpiAwarenessContextFn =
        BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
      auto* set_process_dpi_awareness_context =
          reinterpret_cast<SetProcessDpiAwarenessContextFn>(GetProcAddress(
              user32, "SetProcessDpiAwarenessContext"));
      if (set_process_dpi_awareness_context &&
          (set_process_dpi_awareness_context(
               DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) ||
           GetLastError() == ERROR_ACCESS_DENIED)) {
        return true;
      }
    }

    using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(int);
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
      auto* set_process_dpi_awareness =
          reinterpret_cast<SetProcessDpiAwarenessFn>(
              GetProcAddress(shcore, "SetProcessDpiAwareness"));
      if (set_process_dpi_awareness) {
        constexpr int kProcessPerMonitorDpiAware = 2;
        const HRESULT hr =
            set_process_dpi_awareness(kProcessPerMonitorDpiAware);
        if (SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) {
          FreeLibrary(shcore);
          return true;
        }
      }
      FreeLibrary(shcore);
    }

    using SetProcessDPIAwareFn = BOOL(WINAPI*)();
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
      auto* set_process_dpi_aware =
          reinterpret_cast<SetProcessDPIAwareFn>(
              GetProcAddress(user32, "SetProcessDPIAware"));
      if (set_process_dpi_aware &&
          (set_process_dpi_aware() || GetLastError() == ERROR_ACCESS_DENIED)) {
        return true;
      }
    }
    return false;
  }();
  return enabled;
#endif
}

}  // namespace bo2
