#!/usr/bin/env bash
# Cross-compile the Windows x64 build into build-win/ (Release, D3D12).
#
# Needs: clang-20 tools (clang-cl, lld-link, llvm-rc, llvm-mt, llvm-lib), the
# Windows ReXGlue SDK at sdk-win/ (same version as sdk/), and Microsoft's CRT +
# Windows SDK from xwin at ~/.local/share/xwin (see
# cmake/toolchain-win-clang-cl.cmake). Codegen still runs the Linux sdk/bin/rexglue.
#
# For a ready-to-share folder and zip use scripts/package-windows.sh. To try
# this build directly, copy veronicax.exe and the .dll files to a Windows PC
# and run it: the launcher sets up the game files. Needs the Microsoft Visual
# C++ 2015-2022 x64 redistributable on that PC.
# Usage: scripts/build-win.sh [jobs]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

preset=win-cross-release
jobs="${1:-4}"
out="build-win"

if [ ! -f "$out/CMakeCache.txt" ]; then
    cmake --preset "$preset"
fi
cmake --build --preset "$preset" -j "$jobs"

# The GPU plugin is loaded by name at runtime, so CMake doesn't stage it.
cp -f sdk-win/bin/rexgpu-xenos.dll "$out/"

echo "Windows build ready in $out/:"
ls -1 "$out"/*.exe "$out"/*.dll
