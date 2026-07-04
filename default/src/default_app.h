
// default - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/kernel/xam/module.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <filesystem>
#include <algorithm>
#include <fstream>

#include "../../common/native_input.h"
#include "../../common/project_paths.h"
#include "../../common/android_launcher.h"
#include "../../src/native_renderer/NativeRenderer.h"
#include "../../src/native_renderer/ui/NativeRendererOverlayDialog.h"

namespace bo2 {
void InstallDefaultNativeInput(rex::Runtime* runtime);
void InstallDefaultNativeRenderer(rex::Runtime* runtime);
void InstallDefaultXamOverrides(rex::Runtime* runtime);
}

class DefaultApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<DefaultApp>(new DefaultApp(ctx, "default",
        PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    bo2::ResolveProjectPaths(paths, "default.xex");
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

  void OnPostSetup() override {
    bo2::InstallDefaultXamOverrides(runtime());
    bo2::InstallDefaultNativeInput(runtime());
    bo2::InstallDefaultNativeRenderer(runtime());
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    native_renderer_overlay_ =
        std::make_unique<bo2::native::NativeRendererOverlayDialog>(drawer);
    native_input_.Attach(window());
  }

  void OnGuestThreadExit(rex::system::XThread* thread) override {
    (void)thread;
    TryLaunchRequestedTitle();
  }

  void OnShutdown() override {
    native_renderer_overlay_.reset();
    native_input_.Detach();
    bo2::native::NativeRenderer::Instance().Shutdown();
    TryLaunchRequestedTitle();
  }

 private:
  void TryLaunchRequestedTitle() {
    if (launch_requested_) {
      return;
    }
    auto xam = runtime()->kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
    auto& loader = xam->loader_data();

    REXLOG_INFO("DefaultApp guest exit launch_path='{}' launch_data_present={} launch_data_size={}",
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

    // detect ZM by non-zero byte after first DWORD in launch data
    bool is_zm = false;
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
    {
      auto self_relaunch_count_path =
          cache_root().parent_path() / "android_self_relaunch_count.txt";
      std::error_code ec;
      std::filesystem::remove(self_relaunch_count_path, ec);
    }

    if (loader.launch_data_present && !loader.launch_data.empty()) {
      auto launch_data_path = cache_root().parent_path() / "android_launch_data.bin";
      std::filesystem::create_directories(launch_data_path.parent_path());
      std::ofstream stream(launch_data_path, std::ios::binary | std::ios::trunc);
      stream.write(reinterpret_cast<const char*>(loader.launch_data.data()),
                   static_cast<std::streamsize>(loader.launch_data.size()));
      REXLOG_INFO("DefaultApp wrote Android launch data: {} bytes to {}",
                  loader.launch_data.size(), launch_data_path.string());
    }

    // On Android, we should call back to Java to restart the activity with a new mode.
    REXLOG_INFO("DefaultApp Android restart target app={} mode={}",
                launch_default_mp ? "default_mp" : "default", is_zm ? "zombies" : "");
    bo2::RequestAndroidRestart(launch_default_mp ? "default_mp" : "default", is_zm ? "zombies" : "");
#else
    // Linux/Other
    auto exe = std::filesystem::path(launch_default_mp ? "./default_mp" : "./default");
    std::string cmd = exe.string();
    if (launch_default_mp && is_zm) cmd += " --mode=zombies";
    // system(cmd.c_str()); // Or execve
#endif
  }


  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnShutdown() override {}

  bo2::NativeInput native_input_;
  std::unique_ptr<bo2::native::NativeRendererOverlayDialog>
      native_renderer_overlay_;
  bool launch_requested_ = false;
};
