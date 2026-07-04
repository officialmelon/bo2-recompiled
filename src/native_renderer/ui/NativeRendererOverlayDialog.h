#pragma once

#include <cinttypes>
#include <cstdio>

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>

#include "../NativeRendererStats.h"

namespace bo2::native {

// In-game ImGui overlay showing live native-renderer activity (frame rate,
// draw/shader/PSO counters, live shader translations). Registered from the
// app's OnCreateDialogs; draws nothing until the live backend publishes its
// first frame.
class NativeRendererOverlayDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit NativeRendererOverlayDialog(rex::ui::ImGuiDrawer* drawer)
      : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    const NativeRendererLiveStats stats = GetNativeRendererLiveStats();
    if (!stats.active) {
      return;
    }
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (!ImGui::Begin("Native Renderer", nullptr,
                      ImGuiWindowFlags_AlwaysAutoResize |
                          ImGuiWindowFlags_NoFocusOnAppearing |
                          ImGuiWindowFlags_NoNav)) {
      ImGui::End();
      return;
    }
    ImGui::Text("%s / %s", stats.backend.c_str(), stats.pipeline.c_str());
    ImGui::Text("%.1f FPS (%.2f ms)", stats.fps, stats.frame_time_ms);
    ImGui::Text("frames %" PRIu64 " (failed %" PRIu64 ")",
                stats.frames_submitted, stats.frames_failed);
    ImGui::Text("draws %" PRIu64 " (skipped %" PRIu64 ")",
                stats.draws_last_frame, stats.skipped_draws_last_frame);
    ImGui::Text("shader pairs %" PRIu64 "  live translated %" PRIu64,
                stats.shader_pairs, stats.live_translated_shaders);
    ImGui::Text("PSOs %" PRIu64 " (hit %" PRIu64 " / miss %" PRIu64 ")",
                stats.pso_entries, stats.pso_cache_hits,
                stats.pso_cache_misses);
    ImGui::Text("guest color base 0x%08X", stats.presented_guest_color_base);
    ImGui::End();
  }
};

}  // namespace bo2::native
