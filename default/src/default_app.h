
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

    // detect ZM by non-zero byte after first DWORD in launch data
    bool is_zm = false;
    if (loader.launch_data_present && loader.launch_data.size() > 4) {
        is_zm = std::any_of(loader.launch_data.begin() + 4,
                            loader.launch_data.end(),
                            [](uint8_t b) { return b != 0; });
    }

    bool is_mp = !is_zm && loader.launch_path.find("default_mp") != std::string::npos;
    if (!is_mp && !is_zm) return;

    WCHAR buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    auto mp = std::filesystem::path(buf).parent_path() / "default_mp.exe";

    std::wstring cmdline = mp.wstring();
    if (is_zm) cmdline += L" --mode=zombies";

    STARTUPINFOW si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi = {};
    CreateProcessW(mp.wstring().c_str(), cmdline.data(),
                   nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
}

  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnShutdown() override {}
};