// veronicax - first-run setup and launch settings

#include "launcher/launcher_dialog.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

#include <imgui.h>

#include <rex/logging.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#else
#include <SDL3/SDL_dialog.h>
#endif

#include "launcher/game_files.h"
#include "launcher/xdvdfs.h"

REXCVAR_DEFINE_BOOL(veronicax_show_launcher, true, "VeronicaX",
                    "Show the launcher (setup and display settings) before the game starts");
// Read before the launcher opens (the window's renderer comes from the GPU
// plugin), so a change applies from the next start.
REXCVAR_DEFINE_STRING(veronicax_gpu_backend, "any", "VeronicaX",
                      "Graphics backend: any (D3D12 on Windows), d3d12 or vulkan")
    .allowed({"any", "d3d12", "vulkan"});

namespace veronicax {

namespace {

constexpr float kBaseWidth = 560.0f;

struct WindowSize {
  const char* label;
  int width;
  int height;
};
constexpr std::array<WindowSize, 5> kWindowSizes = {{
    {"Default", 0, 0},
    {"1280 x 720", 1280, 720},
    {"1600 x 900", 1600, 900},
    {"1920 x 1080", 1920, 1080},
    {"2560 x 1440", 2560, 1440},
}};
// As of now resolution scale breaks inventory menu ui
//constexpr std::array<const char*, 3> kScaleLabels = {
//    "1x (native 720p)", "2x (experimental, slower)", "3x (experimental, slowest)"};

void SetFlag(const char* name, const std::string& value) {
  if (!rex::cvar::SetFlagByName(name, value)) {
    REXLOG_WARN("Launcher: could not set {} = {}", name, value);
  }
}

std::string FormatGiB(uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.2f GB", double(bytes) / (1024.0 * 1024.0 * 1024.0));
  return text;
}

// True when `path` is `dir` or somewhere inside it.
bool IsInside(const std::filesystem::path& path, const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::path p = std::filesystem::weakly_canonical(path, ec);
  std::filesystem::path d = std::filesystem::weakly_canonical(dir, ec);
  auto [d_end, p_end] = std::mismatch(d.begin(), d.end(), p.begin(), p.end());
  return !ec && d_end == d.end();
}

using PickedCallback = std::function<void(std::optional<std::string>)>;

#if defined(_WIN32)
// The SDK links a second, uninitialised static SDL into the executable on
// Windows (SDL isn't exported from rexruntime.dll), so use the native
// dialog. It runs on its own thread to keep the window drawing.
void ShowIsoPicker(PickedCallback callback) {
  HWND owner = GetForegroundWindow();
  std::thread([owner, callback = std::move(callback)]() {
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    // Games on Demand header and data files have no extension, and the
    // dialog ignores "*." for those, so the default filter shows everything.
    ofn.lpstrFilter =
        L"Xbox 360 disc image or Games on Demand\0*.*\0Xbox 360 disc image (ISO only)\0*.iso\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = DWORD(std::size(file));
    ofn.lpstrTitle = L"Select your Resident Evil CODE: Veronica X ISO or Games on Demand file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) {
      callback(std::nullopt);
      return;
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(size_t(std::max(size, 1)) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, utf8.data(), size, nullptr, nullptr);
    callback(std::move(utf8));
  }).detach();
}
#else
void SDLCALL OnIsoPicked(void* userdata, const char* const* files, int filter) {
  (void)filter;
  auto* callback = static_cast<PickedCallback*>(userdata);
  if (files && files[0]) {
    (*callback)(std::string(files[0]));
  } else {
    (*callback)(std::nullopt);
  }
  delete callback;
}

void ShowIsoPicker(PickedCallback callback) {
  // Games on Demand header and data files have no extension, so the default
  // filter shows everything.
  static const SDL_DialogFileFilter kFilters[] = {
      {"Xbox 360 disc image or Games on Demand", "*"},
      {"Xbox 360 disc image (ISO only)", "iso"},
  };
  // Owned by the callback, which SDL may invoke on another thread.
  SDL_ShowOpenFileDialog(OnIsoPicked, new PickedCallback(std::move(callback)), nullptr, kFilters,
                         2, nullptr, false);
}
#endif

}  // namespace

LauncherDialog::LauncherDialog(rex::ui::ImGuiDrawer* drawer, Options options,
                               std::function<void()> on_play)
    : ImGuiDialog(drawer),
      options_(std::move(options)),
      on_play_(std::move(on_play)),
      state_(options_.needs_game_files ? State::kChooseIso : State::kReady) {}

LauncherDialog::~LauncherDialog() {
  cancel_ = true;
  if (worker_.joinable()) {
    worker_.join();
  }
}

void LauncherDialog::OnDraw(ImGuiIO& io) {
  // Scale with the window so the text stays readable at 1080p/1440p.
  float scale = std::clamp(io.DisplaySize.y / 720.0f, 1.0f, 3.0f);
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(kBaseWidth * scale, 0.0f), ImGuiCond_Always);
  ImGui::Begin("Project Veronica X", nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
  ImGui::SetWindowFontScale(scale);

  {
    std::lock_guard lock(picked_mutex_);
    if (picked_iso_) {
      std::string iso = std::move(*picked_iso_);
      picked_iso_.reset();
      picker_open_ = false;
      if (!iso.empty()) {
        StartExtraction(PathFromUtf8(iso));
      }
    }
  }

  switch (state_.load()) {
    case State::kChooseIso:
      DrawChooseIso();
      break;
    case State::kWorking:
      DrawWorking();
      break;
    case State::kFailed:
      DrawFailed();
      break;
    case State::kReady:
      DrawSettings();
      break;
  }
  ImGui::End();
}

void LauncherDialog::DrawChooseIso() {
  ImGui::TextWrapped("First-time setup");
  ImGui::Separator();
  ImGui::TextWrapped(
      "Select your Xbox 360 disc image (ISO) of Resident Evil CODE: Veronica X, or the "
      "Games on Demand version: pick the file with the long hex name inside "
      "43430822/00007000 (its .data folder must be next to it). The game files will be "
      "extracted to:");
  ImGui::TextWrapped("%s", PathToUtf8(options_.game_dir).c_str());
  ImGui::TextWrapped("This needs about 2 GB of free space.");
  ImGui::Spacing();
  ImGui::BeginDisabled(picker_open_);
  if (ImGui::Button(picker_open_ ? "Waiting for file picker..." : "Select game file...")) {
    OpenIsoPicker();
  }
  ImGui::EndDisabled();
}

void LauncherDialog::DrawWorking() {
  std::string status;
  {
    std::lock_guard lock(status_mutex_);
    status = status_;
  }
  ImGui::TextWrapped("%s", status.c_str());
  uint64_t total = total_bytes_.load();
  uint64_t done = done_bytes_.load();
  float fraction = total ? float(double(done) / double(total)) : 0.0f;
  std::string overlay = total ? FormatGiB(done) + " / " + FormatGiB(total) : std::string();
  ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay.c_str());
  if (ImGui::Button("Cancel")) {
    cancel_ = true;
  }
}

void LauncherDialog::DrawFailed() {
  std::string error;
  {
    std::lock_guard lock(status_mutex_);
    error = error_;
  }
  ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Setup failed");
  ImGui::Separator();
  ImGui::TextWrapped("%s", error.c_str());
  ImGui::Spacing();
  if (ImGui::Button("Try another file")) {
    state_ = State::kChooseIso;
  }
}

void LauncherDialog::DrawSettings() {
  ImGui::TextWrapped("Display settings");
  ImGui::Separator();

  bool fullscreen = rex::cvar::Query<bool>("fullscreen");
  if (ImGui::RadioButton("Fullscreen", fullscreen)) {
    SetFlag("fullscreen", "true");
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Windowed", !fullscreen)) {
    SetFlag("fullscreen", "false");
  }

  int32_t width = rex::cvar::Query<int32_t>("window_width");
  int32_t height = rex::cvar::Query<int32_t>("window_height");
  size_t size_index = 0;
  for (size_t i = 0; i < kWindowSizes.size(); ++i) {
    if (kWindowSizes[i].width == width && kWindowSizes[i].height == height) {
      size_index = i;
    }
  }
  ImGui::BeginDisabled(fullscreen);
  if (ImGui::BeginCombo("Window size", kWindowSizes[size_index].label)) {
    for (size_t i = 0; i < kWindowSizes.size(); ++i) {
      if (ImGui::Selectable(kWindowSizes[i].label, i == size_index)) {
        SetFlag("window_width", std::to_string(kWindowSizes[i].width));
        SetFlag("window_height", std::to_string(kWindowSizes[i].height));
      }
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
// More relsolution scale setting 
//  int32_t scale = std::clamp(rex::cvar::Query<int32_t>("resolution_scale"), 1,
//                             int32_t(kScaleLabels.size()));
//  if (ImGui::BeginCombo("Internal resolution", kScaleLabels[size_t(scale - 1)])) {
//    for (size_t i = 0; i < kScaleLabels.size(); ++i) {
//      if (ImGui::Selectable(kScaleLabels[i], int32_t(i + 1) == scale)) {
//        SetFlag("resolution_scale", std::to_string(i + 1));
//      }
//    }
//    ImGui::EndCombo();
//  }

#if defined(_WIN32)
  std::string backend = REXCVAR_GET(veronicax_gpu_backend);
  bool vulkan = backend == "vulkan";
  ImGui::TextUnformatted("Graphics");
  ImGui::SameLine();
  if (ImGui::RadioButton("Direct3D 12", !vulkan)) {
    SetFlag("veronicax_gpu_backend", "d3d12");
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Vulkan", vulkan)) {
    SetFlag("veronicax_gpu_backend", "vulkan");
  }
  if (backend != options_.active_gpu_backend &&
      !(backend == "any" && options_.active_gpu_backend == "d3d12")) {
    ImGui::TextDisabled("Restart the game to switch graphics.");
  }
#endif

  bool show_launcher = REXCVAR_GET(veronicax_show_launcher);
  if (ImGui::Checkbox("Show this launcher at startup", &show_launcher)) {
    SetFlag("veronicax_show_launcher", show_launcher ? "true" : "false");
  }
  ImGui::TextDisabled("Settings saved to configuration file next to the launcher.");

  ImGui::Spacing();
  if (ImGui::Button("Play")) {
    Play();
  }
}

void LauncherDialog::OpenIsoPicker() {
  picker_open_ = true;
  // Only reachable from the setup screen, and Play (which deletes this
  // dialog) only exists once a pick has been handled.
  ShowIsoPicker([this](std::optional<std::string> path) {
    std::lock_guard lock(picked_mutex_);
    picked_iso_ = path.value_or(std::string());
  });
}

void LauncherDialog::StartExtraction(std::filesystem::path iso) {
  if (worker_.joinable()) {
    worker_.join();
  }
  cancel_ = false;
  done_bytes_ = 0;
  total_bytes_ = 0;
  {
    std::lock_guard lock(status_mutex_);
    status_ = "Reading " + PathToUtf8(iso.filename()) + "...";
  }
  state_ = State::kWorking;
  worker_ = std::thread([this, iso = std::move(iso)]() { ExtractWorker(iso); });
}

void LauncherDialog::ExtractWorker(std::filesystem::path iso) {
  std::string error;
  auto image = XdvdfsImage::Open(iso, error);
  if (!image) {
    return Fail(error);
  }

  // Check the executable before spending minutes on extraction.
  const XdvdfsImage::Entry* xex = image->Find(kXexName);
  if (!xex) {
    return Fail("This image has no Default.xex, so it is not an Xbox 360 game disc.");
  }
  std::vector<uint8_t> xex_data;
  if (!image->ReadFile(*xex, xex_data)) {
    return Fail("The image is truncated (Default.xex could not be read).");
  }
  uint64_t xex_hash = Fnv1a64(xex_data);
  if (xex_data.size() != kXexSize || xex_hash != kXexFnv1a64) {
    REXLOG_WARN("Launcher: Default.xex fingerprint {} (size {}) does not match {}",
                HexU64(xex_hash), xex_data.size(), HexU64(kXexFnv1a64));
    return Fail(
        "This is not the version of Resident Evil CODE: Veronica X this build supports. "
        "It needs the Xbox 360 game whose Default.xex fingerprint is " +
        HexU64(kXexFnv1a64) + "; this one's is " + HexU64(xex_hash) + ".");
  }

  std::error_code ec;
  const std::filesystem::path& game_dir = options_.game_dir;
  std::filesystem::path staging = game_dir;
  staging += ".partial";
  // Setup replaces the game folder, which would delete a source kept in it.
  if (IsInside(iso, game_dir) || IsInside(iso, staging)) {
    return Fail("The selected file is inside " + PathToUtf8(game_dir) +
                ", which setup replaces. Move it somewhere else first.");
  }
  std::filesystem::create_directories(game_dir.parent_path(), ec);
  auto space = std::filesystem::space(game_dir.parent_path(), ec);
  uint64_t needed = image->total_file_bytes() + (64ull << 20);
  if (!ec && space.available < needed) {
    return Fail("Not enough free space: extraction needs " + FormatGiB(needed) + ", but only " +
                FormatGiB(space.available) + " is free.");
  }

  // Extract into a staging folder and only rename it into place once
  // complete, so an interrupted setup never looks finished.
  std::filesystem::remove_all(staging, ec);
  total_bytes_ = image->total_file_bytes();
  {
    std::lock_guard lock(status_mutex_);
    status_ = "Extracting game files...";
  }
  if (!image->ExtractAll(staging, cancel_, [this](uint64_t done) { done_bytes_ = done; },
                         error)) {
    std::filesystem::remove_all(staging, ec);
    return Fail(error);
  }
  {
    std::ofstream marker(staging / kVerifiedMarker);
    marker << HexU64(kXexFnv1a64) << '\n';
  }
  std::filesystem::remove_all(game_dir, ec);
  std::filesystem::rename(staging, game_dir, ec);
  if (ec) {
    return Fail("Could not move the extracted files into place: " + ec.message());
  }
  REXLOG_INFO("Launcher: extracted {} ({}) into {}", PathToUtf8(iso),
              image->is_games_on_demand() ? "Games on Demand" : "ISO", PathToUtf8(game_dir));
  state_ = State::kReady;
}

void LauncherDialog::Fail(std::string message) {
  REXLOG_ERROR("Launcher: {}", message);
  {
    std::lock_guard lock(status_mutex_);
    error_ = std::move(message);
  }
  state_ = State::kFailed;
}

void LauncherDialog::Play() {
  rex::cvar::SaveConfig(options_.config_path);
  auto on_play = std::move(on_play_);
  Close();
  on_play();
}

}  // namespace veronicax
