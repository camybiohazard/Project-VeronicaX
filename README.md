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
sdk -> rexglue-sdk-.../linux-amd64
```

Keep custom code in new files under `src/`, not in `main.cpp` or
`CMakeLists.txt`, since `rexglue migrate` overwrites those.

## Setup (Linux)

```sh
sudo apt install clang-20 lld-20 libvulkan-dev   # ReXGlue needs Clang 18+; presets expect clang-20
scripts/extract.sh                               # ISO -> game/extracted (once)
```

## Workflow

```sh
scripts/codegen.sh        # analyse Default.xex and generate C++ (~80 s)
scripts/build.sh          # configure and build (dev-relwithdebinfo, -j4)
scripts/run.sh            # run with game_data_root = game/extracted
scripts/smoke-test.sh     # 30 s boot check; run after every codegen change
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

## Windows build (cross-compiled from Linux)

```sh
scripts/build-win.sh      # Release, D3D12, output in build-win/
```

It needs:

- The Windows ReXGlue SDK of the same version as `sdk/`, unpacked so that
  `sdk-win` points at its `win-amd64` folder.
- Microsoft's CRT and Windows SDK, laid out by
  [xwin](https://github.com/Jake-Shadle/xwin):
  `xwin --accept-license --arch x86_64 splat --output ~/.local/share/xwin`.
  Those files are Microsoft-licensed: they stay outside the repo and never go
  into a release.

The toolchain is `cmake/toolchain-win-clang-cl.cmake`. Codegen still runs
the Linux `sdk/bin/rexglue`, via `cmake/win-cross-host-codegen.cmake`.

To run it on Windows, copy `veronicax.exe`, the `.dll` files and
`run-veronicax.bat` from `build-win/`, and put the extracted game files in a
`game` folder next to them. The PC needs the Visual C++ 2015-2022 x64
redistributable.

## Status

- [x] ISO extracted (`Default.xex`, no extra DLL modules)
- [x] Codegen passes validation
- [x] Linux build (Vulkan) boots, plays FMVs, reaches the title screen
- [x] New Game into gameplay; past the barracks window
- [x] Lighter light and shadows in the jail cell
- [x] Windows build (D3D12) cross-compiles and links
- [x] Windows build runs and plays on Windows (GTX 1660, D3D12)
- [ ] Full playthrough (see `notes/known-issues.md`)
