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

## Status

- [x] ISO extracted (`Default.xex`, no extra DLL modules)
- [x] Codegen passes validation (3 boundary overrides)
- [ ] First build with Clang 20
- [ ] Boots to the first kernel or GPU failure
- [ ] Title screen
