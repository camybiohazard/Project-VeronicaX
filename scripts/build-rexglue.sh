#!/usr/bin/env bash
# Build the ReXGlue SDK from source with our patches (patches/README.md) and
# point sdk/ (Linux) and sdk-win/ (Windows cross) at the installs. Both
# platforms then build against and ship the same patched runtime.
#
#   rexglue-sdk-bd833a2/                  source at $commit, patches applied
#     out/install/linux-amd64/            Linux SDK   <- sdk
#     out/install/win-amd64/              Windows SDK <- sdk-win
#
# Needs: podman for linux (the build runs in an Ubuntu 22.04 container); for
# win: clang-cl-20 and Microsoft's CRT/SDK from xwin (cmake/toolchain-win-clang-cl.cmake).
# Safe to rerun: an existing checkout is reused and the patch is only applied
# once. After changing ReXGlue source, regenerate the patch with
#   git -C rexglue-sdk-bd833a2 diff -- include src > patches/rexglue-sdk-bd833a2-veronicax.patch
# Usage: scripts/build-rexglue.sh [linux|win|all] [jobs]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

target="${1:-all}"
jobs="${2:-4}"
commit=bd833a2
src="rexglue-sdk-$commit"
patch="patches/rexglue-sdk-$commit-veronicax.patch"

if [ ! -d "$src/.git" ]; then
    echo "== Cloning ReXGlue $commit"
    git clone --filter=blob:none https://github.com/rexglue/rexglue-sdk.git "$src"
    git -C "$src" checkout -q "$commit"
fi
# catch2 is only for the SDK's tests, moltenvk only for macOS.
mapfile -t modules < <(git -C "$src" config -f .gitmodules --get-regexp '\.path$' |
    awk '{print $2}' | grep -vE 'catch2|moltenvk')
git -C "$src" submodule update --init --recursive -- "${modules[@]}"

if git -C "$src" apply --check "$root/$patch" 2>/dev/null; then
    echo "== Applying $patch"
    git -C "$src" apply "$root/$patch"
elif ! git -C "$src" apply --check --reverse "$root/$patch" 2>/dev/null; then
    echo "$patch neither applies nor is already applied to $src" >&2
    exit 1
fi

linux_sdk_commands() {
    cat <<EOF
set -e
cmake -S "$root/$src" --preset linux-amd64 \
    -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
    -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_DEFAULT_BUILD_TYPE=Release \
    -DCMAKE_CROSS_CONFIGS=Release -DCMAKE_DEFAULT_CONFIGS=Release \
    -DREXGLUE_BUILD_TESTS=OFF > /dev/null
cmake --build "$root/$src/out/build/linux-amd64" --config Release -j $jobs
cmake --install "$root/$src/out/build/linux-amd64" --config Release > /dev/null
EOF
}

build_linux() {
    # Built inside Ubuntu 22.04 (scripts/rexglue-linux.Containerfile) like
    # ReXGlue's releases, so the libraries need only glibc 2.35. A native
    # build (REXGLUE_NATIVE=1) needs this machine's glibc, which is fine for
    # development but makes packages that won't run on older distros.
    if [ "${REXGLUE_NATIVE:-0}" = 1 ]; then
        echo "== Linux SDK (native)"
        bash -c "$(linux_sdk_commands)"
    else
        echo "== Linux SDK (Ubuntu 22.04 container)"
        local image=veronicax-rexglue-build
        podman build -t "$image" -f scripts/rexglue-linux.Containerfile scripts > /dev/null
        # Same path inside as outside, so the installed SDK's paths match.
        podman run --rm -v "$root:$root:z" -w "$root" "$image" bash -c "$(linux_sdk_commands)"
    fi
    ln -sfn "$src/out/install/linux-amd64" sdk
}

build_win() {
    echo "== Windows SDK (cross)"
    local build="$src/out/win-cross" shim="$src/out/case-shim"
    # The xwin headers are lower-case; ReXGlue includes these two in mixed case.
    mkdir -p "$shim"
    printf '#pragma once\n#include <objbase.h>\n' > "$shim/ObjBase.h"
    printf '#pragma once\n#include <sdkddkver.h>\n' > "$shim/SDKDDKVer.h"
    if [ ! -f "$build/CMakeCache.txt" ]; then
        cmake -S "$src" -B "$build" -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchain-win-clang-cl.cmake" \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$root/$src/out/install/win-amd64" \
            -DREXGLUE_USE_VULKAN=ON -DREXGLUE_BUILD_TESTS=OFF \
            -DSDL_HIDAPI_LIBUSB=OFF > /dev/null  # else SDL finds the host libusb
        # Append rather than pass CMAKE_*_FLAGS on the first configure, which
        # would replace the toolchain's xwin include paths. clang-cl ignores
        # the SDK's -fno-char8_t, hence /Zc:char8_t-.
        local cflags cxxflags
        cflags="$(sed -n 's/^CMAKE_C_FLAGS:STRING=//p' "$build/CMakeCache.txt")"
        cxxflags="$(sed -n 's/^CMAKE_CXX_FLAGS:STRING=//p' "$build/CMakeCache.txt")"
        cmake "$build" -DCMAKE_C_FLAGS="$cflags /imsvc $root/$shim" \
            -DCMAKE_CXX_FLAGS="$cxxflags /imsvc $root/$shim /Zc:char8_t-" > /dev/null
    fi
    cmake --build "$build" -j "$jobs"
    cmake --install "$build" > /dev/null
    # ReXGlue's install registers itself in the CMake user package registry,
    # where any project's find_package(rexglue) would pick up this Windows SDK.
    # Our presets ignore the registry; drop the entry for other projects too.
    grep -lxF "$root/$src/out/install/win-amd64" ~/.cmake/packages/rexglue/* 2>/dev/null |
        xargs -r rm -f
    ln -sfn "$src/out/install/win-amd64" sdk-win
}

case "$target" in
    linux) build_linux ;;
    win) build_win ;;
    all) build_linux; build_win ;;
    *) echo "usage: $0 [linux|win|all] [jobs]" >&2; exit 1 ;;
esac
echo "sdk -> $(readlink sdk)"
echo "sdk-win -> $(readlink sdk-win 2>/dev/null || echo none)"
