
// default_mp - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/system/kernel_state.h>
#include <rex/kernel/xam/module.h>
#include <windows.h>
#include <filesystem>
#include <rex/cvar.h>

class DefaultMpApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

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

  // Override virtual hooks for customization:
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
