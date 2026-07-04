#pragma once

#include <rex/rex_app.h>
#include <rex/system/kernel_state.h>
#include <rex/kernel/xam/module.h>
#include <rex/cvar.h>
#include <rex/memory/utils.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iterator>

#include "../../common/native_input.h"
#include "../../common/project_paths.h"
#include "../../common/android_launcher.h"
#include "../../src/native_renderer/NativeRenderer.h"
#include "../../src/native_renderer/ui/NativeRendererOverlayDialog.h"

REXCVAR_DECLARE(std::string, mode);

namespace default_mp {
void InstallXamOverrides(rex::Runtime* runtime);
}

namespace bo2 {
void InstallDefaultMpNativeInput(rex::Runtime* runtime);
void InstallDefaultMpNativeRenderer(rex::Runtime* runtime);
}

class DefaultMpApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static constexpr uint32_t kBootModeFlagsAddr = 0x83663530;
  static constexpr uint32_t kBootModeZombiesBit = 1u << 4;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<DefaultMpApp>(new DefaultMpApp(ctx, "default_mp",
        PPCImageConfig));
  }

  void OnLoadXexImage(std::string& xex_image) override {
    xex_image = "game:\\default_mp.xex";
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    bo2::ResolveProjectPaths(paths, "default_mp.xex");
  }

  void OnPostSetup() override {
    default_mp::InstallXamOverrides(runtime());
    bo2::InstallDefaultMpNativeInput(runtime());
    bo2::InstallDefaultMpNativeRenderer(runtime());
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    rex::cvar::SetFlagByName("vsync", "false");
    rex::cvar::SetFlagByName("video_mode_width", "1280");
    rex::cvar::SetFlagByName("video_mode_height", "720");
    rex::cvar::SetFlagByName("video_mode_refresh_rate", "60");
    rex::cvar::SetFlagByName("draw_resolution_scale_x", "1");
    rex::cvar::SetFlagByName("draw_resolution_scale_y", "1");
    rex::cvar::SetFlagByName("native_2x_msaa", "false");
    rex::cvar::SetFlagByName("present_effect", "bilinear");
    rex::cvar::SetFlagByName("async_shader_compilation", "true");
#if defined(__ANDROID__)
    rex::cvar::SetFlagByName("store_shaders", "false");
#endif
    rex::cvar::SetFlagByName("vulkan_async_skip_incomplete_frames", "false");
    rex::cvar::SetFlagByName("vulkan_dynamic_rendering", "false");
    rex::cvar::SetFlagByName("vulkan_pipeline_creation_threads", "1");
    rex::cvar::SetFlagByName("audio_maxqframes", "10");
    rex::cvar::SetFlagByName("d3d12_adapter", "1");
    rex::cvar::SetFlagByName("ignore_thread_priorities", "true");
    rex::cvar::SetFlagByName("mnk_mode", "false");
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    native_renderer_overlay_ =
        std::make_unique<bo2::native::NativeRendererOverlayDialog>(drawer);
    native_input_.Attach(window());
  }

  void OnPreLaunchModule() override {
    LoadAndroidLaunchData();

    if (REXCVAR_GET(mode) != "zombies") {
      return;
    }

#if defined(__ANDROID__)
    REXLOG_INFO(
        "DefaultMpApp Android zombies prelaunch complete; using launch data seed");
    return;
#else
    ApplyZombiesBootFlag("prelaunch");
#endif
  }

  void OnPostLaunchModule(rex::system::XThread* thread) override {
    (void)thread;
#if defined(__ANDROID__)
    if (REXCVAR_GET(mode) == "zombies") {
      ApplyZombiesBootFlag("post-prepare");
    }
#endif
  }

  void OnGuestThreadExit(rex::system::XThread* thread) override {
    (void)thread;
    TryLaunchRequestedTitle();
  }

  // launch mp/zm/sp based on launch_data & xex name.
  void OnShutdown() override {
    native_renderer_overlay_.reset();
    native_input_.Detach();
    bo2::native::NativeRenderer::Instance().Shutdown();
    TryLaunchRequestedTitle();
  }

 private:
  void LoadAndroidLaunchData() {
#if defined(__ANDROID__)
    auto launch_data_path = cache_root().parent_path() / "android_launch_data.bin";
    std::ifstream stream(launch_data_path, std::ios::binary);
    if (!stream) {
      SeedAndroidZombiesLaunchDataIfNeeded("no handoff file");
      return;
    }

    std::vector<uint8_t> launch_data(
        (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (launch_data.empty()) {
      SeedAndroidZombiesLaunchDataIfNeeded("empty handoff file");
      return;
    }

    auto xam = runtime()->kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
    auto& loader = xam->loader_data();
    loader.launch_data_present = true;
    loader.launch_data = std::move(launch_data);
    std::error_code ec;
    std::filesystem::remove(launch_data_path, ec);
    REXLOG_INFO("DefaultMpApp loaded Android launch data: {} bytes from {}",
                loader.launch_data.size(), launch_data_path.string());
    if (ec) {
      REXLOG_INFO("DefaultMpApp could not remove Android launch data handoff {}: {}",
                  launch_data_path.string(), ec.message());
    }
#endif
  }

  void SeedAndroidZombiesLaunchDataIfNeeded(const char* reason) {
#if defined(__ANDROID__)
    if (REXCVAR_GET(mode) != "zombies") {
      return;
    }

    auto xam = runtime()->kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
    auto& loader = xam->loader_data();
    if (loader.launch_data_present && !loader.launch_data.empty()) {
      return;
    }

    loader.launch_path = "game:\\default_mp.xex";
    loader.launch_data_present = true;
    loader.launch_data = {0x00, 0x00, 0x00, 0x0B};
    REXLOG_INFO("DefaultMpApp seeded Android zombies launch data ({})", reason);
#else
    (void)reason;
#endif
  }

  void ApplyZombiesBootFlag(const char* phase) {
    auto* flags = runtime()->memory()->TranslateVirtual(kBootModeFlagsAddr);
    auto value = rex::memory::load_and_swap<uint32_t>(flags);
    const auto old_value = value;
    value |= kBootModeZombiesBit;
    rex::memory::store_and_swap<uint32_t>(flags, value);
    REXLOG_INFO("DefaultMpApp zombies boot flag {}: {:08X} -> {:08X}",
                phase, old_value, value);
  }

  void TryLaunchRequestedTitle() {
    if (launch_requested_) {
      return;
    }
    auto xam = runtime()->kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
    auto& loader = xam->loader_data();

    REXLOG_INFO("DefaultMpApp guest exit launch_path='{}' launch_data_present={} launch_data_size={}",
                loader.launch_path, loader.launch_data_present, loader.launch_data.size());
    if (loader.launch_path.empty()) {
#if defined(__ANDROID__)
      if (!loader.launch_data_present || loader.launch_data.empty()) {
        return;
      }
#else
      return;
#endif
    }
    bool is_default = loader.launch_path.find("default.xex") != std::string::npos;
    bool is_default_mp = loader.launch_path.find("default_mp.xex") != std::string::npos;
#if defined(__ANDROID__)
    if (loader.launch_path.empty() && loader.launch_data_present && !loader.launch_data.empty()) {
      is_default_mp = true;
    }
#endif
    if (!is_default && !is_default_mp) return;

    // are we booting zombies?
    bool is_zm = false;
    // base off the launch_data
    if (loader.launch_data_present && loader.launch_data.size() >= 4) {
      is_zm = loader.launch_data[3] == 0x0B;
    }

    const bool launch_default_mp = is_default_mp || is_zm;
    launch_requested_ = true;

#if defined(_WIN32)
    WCHAR buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    auto exe_dir = std::filesystem::path(buf).parent_path();
    auto exe = exe_dir / (launch_default_mp ? "default_mp.exe" : "default.exe");

    std::wstring cmdline = exe.wstring();
    if (launch_default_mp && is_zm) cmdline += L" --mode=zombies";

    STARTUPINFOW si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi = {};
    CreateProcessW(exe.wstring().c_str(), cmdline.data(),
                   nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
#elif defined(__ANDROID__)
    // On Android, we should call back to Java to restart the activity with a new mode.
    const char* restart_app = launch_default_mp ? "default_mp" : "default";
    const char* restart_mode = is_zm ? "zombies" : "";
    REXLOG_INFO("DefaultMpApp Android restart target app={} mode={}",
                restart_app, restart_mode);
    bo2::RequestAndroidRestart(restart_app, restart_mode);
#else
    // Linux/Other
    auto exe = std::filesystem::path(launch_default_mp ? "./default_mp" : "./default");
    std::string cmd = exe.string();
    if (launch_default_mp && is_zm) cmd += " --mode=zombies";
    // system(cmd.c_str());
#endif
  }


  bo2::NativeInput native_input_;
  std::unique_ptr<bo2::native::NativeRendererOverlayDialog>
      native_renderer_overlay_;
  bool launch_requested_ = false;
};
