
// default - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/system/kernel_state.h>
#include <rex/kernel/xam/module.h>
#include <windows.h>
#include <filesystem>

class DefaultApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<DefaultApp>(new DefaultApp(ctx, "default",
        PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    paths.update_data_root = paths.game_data_root;
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    rex::cvar::SetFlagByName("vsync", "false");
    rex::cvar::SetFlagByName("ignore_thread_priorities", "false");
  }

  void OnShutdown() override {
    auto xam = runtime()->kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
    auto& loader = xam->loader_data();

    if (loader.launch_path.empty()) return;

    // feels messy

    if (loader.launch_path.find("default_mp") != std::string::npos) {
      WCHAR buf[MAX_PATH];
      GetModuleFileNameW(nullptr, buf, MAX_PATH);
      std::filesystem::path exe(buf);
      auto config = exe.parent_path().filename();
      auto mp = exe.parent_path() / "default_mp.exe";

      STARTUPINFOW si = {};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi = {};
      CreateProcessW(mp.wstring().c_str(), nullptr, nullptr, nullptr,
                     FALSE, 0, nullptr, nullptr, &si, &pi);
    }
  }

  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnShutdown() override {}
};
