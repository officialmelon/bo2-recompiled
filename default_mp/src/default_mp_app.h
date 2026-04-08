#pragma once

#include <rex/rex_app.h>
#include <rex/system/kernel_state.h>
#include <rex/kernel/xam/module.h>
#include <rex/cvar.h>
#include <rex/memory/utils.h>
#include <windows.h>
#include <filesystem>
#include <algorithm>

REXCVAR_DECLARE(std::string, mode);

namespace default_mp {
void InstallXamOverrides(rex::Runtime* runtime);
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
    xex_image = "game:/default_mp.xex";
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    paths.update_data_root = paths.game_data_root;
  }

  void OnPostSetup() override {
    default_mp::InstallXamOverrides(runtime());
  }

  void OnPreLaunchModule() override {
    if (REXCVAR_GET(mode) != "zombies") {
      return;
    }

    auto* flags = runtime()->memory()->TranslateVirtual(kBootModeFlagsAddr);
    auto value = rex::memory::load_and_swap<uint32_t>(flags);
    value |= kBootModeZombiesBit;
    rex::memory::store_and_swap<uint32_t>(flags, value);
  }

  // launch mp/zm/sp based on launch_data & xex name.
  void OnShutdown() override {
    auto xam = runtime()->kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
    auto& loader = xam->loader_data();

    if (loader.launch_path.empty()) return;
    bool is_default = loader.launch_path.find("default.xex") != std::string::npos;
    bool is_default_mp = loader.launch_path.find("default_mp.xex") != std::string::npos;
    if (!is_default && !is_default_mp) return;

    // are we booting zombies?
    bool is_zm = false;
    // base off the launch_data
    if (loader.launch_data_present && loader.launch_data.size() > 4) {
      is_zm = std::any_of(loader.launch_data.begin() + 4, loader.launch_data.end(),
                          [](uint8_t b) { return b != 0; });
    }

    WCHAR buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    auto exe_dir = std::filesystem::path(buf).parent_path();
    auto exe = exe_dir / (is_default_mp ? "default_mp.exe" : "default.exe");

    std::wstring cmdline = exe.wstring();
    if (is_default_mp && is_zm) cmdline += L" --mode=zombies";

    STARTUPINFOW si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi = {};
    CreateProcessW(exe.wstring().c_str(), cmdline.data(),
                   nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
  }
};
