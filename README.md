# Project VeronicaX

A static recompilation of *Resident Evil – Code: Veronica X* (Xbox 360) built
with [ReXGlue](https://mintlify.wiki/rexglue/rexglue-sdk) 0.10.0.

## Layout

```
CMakeLists.txt            SDK-managed (rexglue init / migrate overwrite it)
CMakePresets.json         SDK-managed
CMakeUserPresets.json     ours: dev-* presets that point CMake at ./sdk
veronicax_manifest.toml   codegen manifest (project, XEX, includes)
config/functions.toml     manual function boundary overrides
src/main.cpp              SDK-managed entry point
src/veronicax_app.h       ReXApp subclass: put hooks and overrides here
generated/rexglue.cmake   SDK-managed CMake glue (committed)
generated/default/        codegen output (gitignored, rebuilt on demand)
scripts/                  extract / codegen / build / run helpers
notes/                    research: function notes, ISO listing, logs
game/                     the ISO and extracted files (gitignored)
patches/                  our ReXGlue source patches (see patches/README.md)
rexglue-sdk-bd833a2/      ReXGlue source, built by scripts/build-rexglue.sh (gitignored)
sdk -> rexglue-sdk-bd833a2/out/install/linux-amd64
sdk-win -> rexglue-sdk-bd833a2/out/install/win-amd64
```

Keep custom code in new files under `src/`, not in `main.cpp` or
`CMakeLists.txt`, since `rexglue migrate` overwrites those.

## Setup (Linux)

```sh
sudo apt install clang-20 lld-20 libvulkan-dev libx11-xcb-dev libwayland-dev
scripts/build-rexglue.sh linux                   # ReXGlue from source + our patches -> sdk/
scripts/extract.sh                               # ISO -> game/extracted (once)
```

ReXGlue is built from source at bd833a2, the 0.10.0.24 SDK commit, with
`patches/` applied, instead of using the prebuilt SDK. The patch fixes a
runtime bug (see patches/README.md), and Linux and Windows share it.
`scripts/build-rexglue.sh` clones, patches, builds and points `sdk`/`sdk-win`
at the installs. Rerun it after changing the patch.

## Workflow

```sh
scripts/codegen.sh        # analyse Default.xex and generate C++ (~80 s)
scripts/build.sh          # configure and build (dev-relwithdebinfo, -j4)
scripts/run.sh            # run with game_data_root = game/extracted
scripts/smoke-test.sh     # boot check (up to 90 s); run after every codegen change
```

When codegen lists unresolved targets, add a `[functions."0x..."]` entry to
`config/functions.toml`, record why in `notes/functions.md`, and rerun.
Afterwards run `scripts/scan_gaps.sh`, which regenerates `config/gaps.toml`
(functions only reached through pointers; see `notes/functions.md`). It
needs `game/image.bin`, which `scripts/dump_image.sh` creates once from a
built binary.

For crashes, freezes and hangs, use `scripts/debug-run.sh`. It runs the game
under gdb and writes the faulting thread, guest registers, the guest object
and its vtable, and every stack to `notes/logs/hang-backtrace.txt`.

## Launcher

Without a verified `game/` folder next to the executable, the game window
opens on a setup screen. It asks for the ISO or a Games on Demand package
(the header file in `43430822/00007000/`, or any `DataNNNN` file next to it),
checks that its `Default.xex` is the version this build was recompiled from,
and extracts the disc into `game/` (staging into `game.partial/`, then
renaming). It refuses a source inside `game/`, since that folder is replaced. After that, every launch shows
a settings screen (fullscreen or windowed, window size, internal resolution)
with a Play button. VSync is always on: the game runs too fast without it.
Choices are saved to `veronicax.toml` next to the executable; F4 in game has
the full ReXGlue settings.

Saves are not next to the executable. They go in ReXGlue's user data folder,
`~/.local/share/veronicax/` on Linux (`$XDG_DATA_HOME/veronicax/` if set) and
`Documents\veronicax\` on Windows, so they survive rebuilds and
re-extraction. `B13EBABEBABEBABE/43430822/` holds the saves (the emulated
profile's ID, then the game's title ID), `achievements/` the achievements and
`cache/` the shader cache. `--user_data_root` moves it.

- Source: `src/launcher/` (`xdvdfs.*` is the disc reader, a port of
  `scripts/xiso_extract.py`, plus the Games on Demand (SVOD) layout).
- Skip it with `--veronicax_show_launcher=false`. The dev scripts do, and
  they pass `--game_data_root`, which skips the setup step.

## Release package (Linux)

```sh
scripts/fetch-licenses.sh            # once per SDK version: third_party_licenses/
scripts/package-linux.sh [version]   # dist/VeronicaX-<version>-linux-x86_64.tar.gz
```

This builds the `dist-linux` preset (Release with `-fno-math-errno`, which
keeps the game at the SDK's glibc 2.35 / libstdc++ 3.4.32 floor) and stages
the program, both SDK libraries, a player README, `LICENSE` and
`third_party_licenses/`. The script then checks the package: no game data
or personal files, every library resolving from the package or the system,
and nothing above the glibc floor. Finally it writes the `.tar.gz` and a
`.sha256`.

## Release package (Windows)

```sh
scripts/package-windows.sh [version] # dist/VeronicaX-<version>-windows-x86_64.zip
```

This cross-builds `win-cross-release` (see below) and stages `veronicax.exe`,
`rexruntime.dll`, `rexgpu-xenos.dll`, a player README (CRLF), `LICENSE` and
`third_party_licenses/`. The script then checks the package: no game data,
personal files or Microsoft SDK/CRT files; every imported DLL either bundled,
part of Windows, or part of the VC++ redistributable; all binaries x64.
Finally it writes the `.zip` and a `.sha256`. On Windows the launcher uses the
native file dialog, because the SDK links a separate static SDL into the
executable there.

## Windows build (cross-compiled from Linux)

```sh
scripts/build-win.sh      # Release, D3D12 + Vulkan, output in build-win/
```

It needs:

- The Windows ReXGlue SDK, cross-built from the same source:
  `scripts/build-rexglue.sh win` (sets up `sdk-win`). It includes the Vulkan
  backend, which the official Windows SDK leaves out.
- Microsoft's CRT and Windows SDK, laid out by
  [xwin](https://github.com/Jake-Shadle/xwin):
  `xwin --accept-license --arch x86_64 splat --output ~/.local/share/xwin`.
  Those files are Microsoft-licensed: they stay outside the repo and never go
  into a release.

The toolchain is `cmake/toolchain-win-clang-cl.cmake`. Codegen still runs
the Linux `sdk/bin/rexglue`, via `cmake/win-cross-host-codegen.cmake`.

To try a build directly on Windows, copy `veronicax.exe` and the `.dll` files
from `build-win/` and run it; the launcher sets up the game files. The PC
needs the Visual C++ 2015-2022 x64 redistributable.

To try a Windows package on Linux, `scripts/run-proton.sh [package folder]`
runs it under Steam's Proton Experimental (Steam needn't be open). It
defaults to the newest `dist/VeronicaX-*-windows-x86_64` and keeps its Wine
prefix, and so the Windows saves, in `~/.local/share/veronicax-proton`. In
the launcher, Linux files are on `Z:`.

## Status

- [x] ISO extracted (`Default.xex`, no extra DLL modules)
- [x] Codegen passes validation
- [x] Linux build (Vulkan) boots, plays FMVs, reaches the title screen
- [x] New Game into gameplay; past the barracks window
- [x] Lighter light and shadows in the jail cell
- [x] Windows build (D3D12) cross-compiles and links
- [x] Windows build runs and plays on Windows (GTX 1660, D3D12)
- [ ] Full playthrough (see `notes/known-issues.md`)
