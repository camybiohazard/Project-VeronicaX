#!/usr/bin/env bash
# Cross-compile the Windows x64 build into build-win/ (Release, D3D12).
#
# Needs: clang-20 tools (clang-cl, lld-link, llvm-rc, llvm-mt, llvm-lib), the
# Windows ReXGlue SDK at sdk-win/ (same version as sdk/), and Microsoft's CRT +
# Windows SDK from xwin at ~/.local/share/xwin (see
# cmake/toolchain-win-clang-cl.cmake). Codegen still runs the Linux sdk/bin/rexglue.
#
# Copy build-win/veronicax.exe, the .dll files next to it and
# run-veronicax.bat to a Windows PC; put the extracted game files in a "game"
# folder beside them. Needs the Microsoft Visual C++ 2015-2022 x64
# redistributable on that PC.
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

cat > "$out/run-veronicax.bat" <<'EOF'
@echo off
rem Runs VeronicaX with the game files from the "game" folder next to this file.
"%~dp0veronicax.exe" --game_data_root="%~dp0game" %*
EOF

echo "Windows build ready in $out/:"
ls -1 "$out"/*.exe "$out"/*.dll "$out"/*.bat
