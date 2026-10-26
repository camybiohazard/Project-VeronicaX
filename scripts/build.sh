#!/usr/bin/env bash
# Configure (first time) and build. Usage: scripts/build.sh [preset] [jobs]
# The generated code is huge; lower jobs if the machine runs out of RAM.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

preset="${1:-dev-relwithdebinfo}"
jobs="${2:-4}"

if [ ! -f "out/build/$preset/CMakeCache.txt" ]; then
    cmake --preset "$preset"
fi
cmake --build --preset "$preset" -j "$jobs"

# The binary's RUNPATH is $ORIGIN, but the SDK only stages its shared
# libraries next to the binary on Windows/macOS, so link them in on Linux.
for lib in "$root"/sdk/lib/*.so; do
    ln -sfn "$lib" "out/build/$preset/$(basename "$lib")"
done
