// veronicax - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/rex_app.h>

#include "launcher/game_files.h"
#include "launcher/launcher_dialog.h"

class VeronicaxApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<VeronicaxApp>(new VeronicaxApp(ctx, "veronicax",
        PPCImageConfig));
  }

  // Game files live in "game" next to the executable unless
  // --game_data_root points elsewhere (as the dev scripts do).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    game_root_from_flag_ = !paths.game_data_root.empty();
    if (!game_root_from_flag_) {
      paths.game_data_root = rex::filesystem::GetExecutableFolder() / "game";
    }
  }

  // Shows the launcher before the runtime starts: ISO setup when the game
  // files are missing, otherwise display settings and Play.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
    bool have_game =
        game_root_from_flag_
            ? std::filesystem::is_regular_file(defaults.game_data_root /
                                               veronicax::kXexName)
            : veronicax::IsVerifiedGameFolder(defaults.game_data_root);
    if (have_game && !REXCVAR_GET(veronicax_show_launcher)) {
      return defaults;
    }
    veronicax::LauncherDialog::Options options{
        defaults.game_data_root, defaults.config_path, !have_game};
    // The dialog deletes itself when closed; resume after its frame ends.
    new veronicax::LauncherDialog(
        imgui_drawer(), std::move(options), [this, defaults, resume]() {
          app_context().CallInUIThreadDeferred(
              [defaults, resume]() { resume(defaults); });
        });
    return std::nullopt;
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
    // shadows in the opening jail cell (found via VeronicaRecomp's settings).
    // Applied only when no config file, environment variable or flag set it
    // (checked by source, since "true" is also the compiled-in default).
    if (rex::cvar::GetFlagSource("snorm16_render_target_full_range") ==
        rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName("snorm16_render_target_full_range", "false");
    }
    REXLOG_INFO("snorm16_render_target_full_range = {}",
                rex::cvar::GetFlagByName("snorm16_render_target_full_range"));
    // The game's timing assumes the display's refresh, so without VSync it
    // runs too fast. Forced on, even over a saved config from older builds.
    if (!rex::cvar::Query<bool>("vsync")) {
      REXLOG_WARN("vsync must stay on for correct game speed; enabling it");
      rex::cvar::SetFlagByName("vsync", "true");
    }
    UnlinkGuestMemoryFile();
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPostLoadXexImage() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}

 private:
  // The runtime backs guest memory with a /dev/shm/xenia_memory_* file and
  // only unlinks it on a clean shutdown, but it always hard-exits, so every
  // run left one behind (several GB of RAM after a few dozen runs, then bus
  // errors). The mapping only uses the open descriptor, so drop the name now;
  // the kernel frees it when the process ends, however it ends.
  static void UnlinkGuestMemoryFile() {
#if defined(__linux__)
    std::error_code ec;
    for (const auto& fd : std::filesystem::directory_iterator("/proc/self/fd", ec)) {
      std::filesystem::path target = std::filesystem::read_symlink(fd.path(), ec);
      if (!ec && target.parent_path() == "/dev/shm" &&
          target.filename().string().starts_with("xenia_memory_")) {
        std::filesystem::remove(target, ec);
      }
    }
#endif
  }

  bool game_root_from_flag_ = false;
};
