// veronicax - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/rex_app.h>

class VeronicaxApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<VeronicaxApp>(new VeronicaxApp(ctx, "veronicax",
        PPCImageConfig));
  }

  // The disc ships "Default.xex"; the runtime asks for "default.xex", which
  // only resolves on case-insensitive host filesystems.
  void OnLoadXexImage(std::string& xex_image) override {
    xex_image = "game:\\Default.xex";
  }

  // Without a GPU plugin every Vd* call is ignored and nothing is drawn.
  // --gpu_plugin still overrides this.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
  }

  // GPU cvars are registered by the GPU plugin, so they only exist after
  // setup; the render target cache reads this one per draw and resolve.
  void OnPostSetup() override {
    // Full-range SNORM16 render targets break the lighter's light and
    // Applied only when no config file, environment variable or flag set it
    // (checked by source, since "true" is also the compiled-in default).
    if (rex::cvar::GetFlagSource("snorm16_render_target_full_range") ==
        rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName("snorm16_render_target_full_range", "false");
    }
    REXLOG_INFO("snorm16_render_target_full_range = {}",
                rex::cvar::GetFlagByName("snorm16_render_target_full_range"));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPostLoadXexImage() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
