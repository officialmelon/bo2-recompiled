
// default - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <windows.h>
#include <cstdio>

class DefaultApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<DefaultApp>(new DefaultApp(ctx, "default",
        PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    // game needs to resolve files from update
    paths.update_data_root = paths.game_data_root;
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // PERF: WAIT_REG_MEM with vsync=true sleeps (wait/256) ms per failed check.
    // BO2 uses wait=0x6400 (25600) for its CPU-GPU frame sync packets -> 100ms
    // sleep per iteration -> ~3 FPS. Setting vsync=false switches WAIT_REG_MEM
    // to MaybeYield() and fires the vblank interrupt every 1ms instead of
    // 16.67ms, letting the game run at full speed.
    rex::cvar::SetFlagByName("vsync", "false");

    // PERF: BO2 uses thread priorities to ensure its render/worker threads
    // get scheduled correctly. With all threads at equal Windows priority
    // the scheduler can starve important threads.
    rex::cvar::SetFlagByName("ignore_thread_priorities", "false");
  }

  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnShutdown() override {}
};
