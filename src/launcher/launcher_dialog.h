// veronicax - first-run setup and launch settings
//
// Shown in the game window before the runtime starts (ReXApp's
// OnFinalizePaths hook). Without a verified game folder it asks for the ISO
// or Games on Demand package, checks it is the version this build was
// recompiled from, and extracts it next to the executable. Then it offers a few display settings, saved to the
// config file, and a Play button.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>

REXCVAR_DECLARE(bool, veronicax_show_launcher);
REXCVAR_DECLARE(std::string, veronicax_gpu_backend);

namespace veronicax {

class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  struct Options {
    std::filesystem::path game_dir;
    std::filesystem::path config_path;
    bool needs_game_files = false;
    // Backend the GPU plugin was created with ("d3d12" or "vulkan").
    std::string active_gpu_backend;
  };

  // `on_play` runs on the UI thread after the dialog closes.
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, Options options, std::function<void()> on_play);
  ~LauncherDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class State { kChooseIso, kWorking, kFailed, kReady };

  void DrawChooseIso();
  void DrawWorking();
  void DrawFailed();
  void DrawSettings();
  void OpenIsoPicker();
  void StartExtraction(std::filesystem::path iso);
  void ExtractWorker(std::filesystem::path iso);
  void Fail(std::string message);
  void Play();

  Options options_;
  std::function<void()> on_play_;
  std::atomic<State> state_;

  // Filled by the SDL file dialog callback, which may run on another thread.
  std::mutex picked_mutex_;
  std::optional<std::string> picked_iso_;
  bool picker_open_ = false;

  std::thread worker_;
  std::atomic<bool> cancel_{false};
  std::atomic<uint64_t> done_bytes_{0};
  std::atomic<uint64_t> total_bytes_{0};
  std::mutex status_mutex_;
  std::string status_;
  std::string error_;
};

}  // namespace veronicax
