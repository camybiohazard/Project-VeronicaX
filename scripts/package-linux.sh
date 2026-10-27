#!/usr/bin/env bash
# Build Release and assemble a ready-to-share Linux folder plus .tar.gz in dist/.
#
#   dist/VeronicaX-<version>-linux-x86_64/
#     veronicax               the game (opens on the launcher)
#     librexruntime.so        ReXGlue runtime  } found via RPATH $ORIGIN
#     librexgpu-xenos.so      GPU plugin       }
#     README.txt              player instructions
#     LICENSE                 this project's licence (if the repo has one)
#     third_party_licenses/   ReXGlue + bundled libraries (scripts/fetch-licenses.sh)
#
# No game data is ever included: players supply their own ISO through the
# launcher. The script fails if anything game-derived ends up in the folder.
# Usage: scripts/package-linux.sh [version] [jobs]
#   version defaults to 0.1.0-<git short hash>[-dirty]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

version="${1:-0.1.0-$(git describe --always --dirty)}"
jobs="${2:-4}"
# -fno-math-errno: without it the generated code calls sqrtf, which links the
# glibc 2.43 symbol version and would shut out every older distribution.
preset=dist-linux
build="out/build/$preset"
name="VeronicaX-$version-linux-x86_64"
stage="dist/$name"

[ -d third_party_licenses ] || { echo "third_party_licenses/ missing: run scripts/fetch-licenses.sh" >&2; exit 1; }

echo "== Building $preset"
scripts/build.sh "$preset" "$jobs" > /dev/null

echo "== Staging $stage"
rm -rf "$stage" "dist/$name.tar.gz" "dist/$name.tar.gz.sha256"
mkdir -p "$stage"
install -m 755 "$build/veronicax" "$stage/veronicax"
strip --strip-unneeded "$stage/veronicax"
for lib in librexruntime.so librexgpu-xenos.so; do
    install -m 755 "$(readlink -f "sdk/lib/$lib")" "$stage/$lib"
done
cp -r third_party_licenses "$stage/"
[ -f LICENSE ] && cp LICENSE "$stage/LICENSE"

cat > "$stage/README.txt" <<EOF
VeronicaX $version - Resident Evil CODE: Veronica X (Xbox 360), recompiled
for Linux x86-64.

This package contains no game data. You need your own Xbox 360 disc image
(ISO) of Resident Evil CODE: Veronica X.

Getting started
  1. Extract this folder anywhere you can write to (about 2 GB free needed).
  2. Run ./veronicax
  3. On first launch, click "Select ISO..." and choose your disc image. The
     launcher checks it is the supported version and extracts it into a
     "game" folder here.
  4. Pick your display settings and press Play.

Later launches go straight to the settings screen; untick "Show this launcher
at startup" to skip it. Press F4 in game for more options.

Files created next to the program
  game/            extracted game files (from your ISO)
  veronicax.toml   your settings
  logs/            log files: include the newest one when reporting a problem
Saves and caches go in ~/.local/share/veronicax/.

Requirements
  - 64-bit x86 CPU with SSE4.1
  - A GPU with an up-to-date Vulkan driver (Mesa, or the NVIDIA driver)
  - Wayland or X11 desktop; the file picker uses xdg-desktop-portal (or zenity)

Built with the ReXGlue SDK (https://github.com/rexglue/rexglue-sdk), which
includes code derived from Xenia (https://xenia.jp). See third_party_licenses/.
EOF

echo "== Checking the package"
fail=0
# 1. Nothing game-derived or personal.
leaks="$(find "$stage" \( -iname '*.xex' -o -iname '*.iso' -o -iname 'image.bin' -o -iname '*.toml' \
    -o -iname '*.log' -o -iname 'game-manifest.txt' -o -name game -o -name logs \) -print)"
if [ -n "$leaks" ]; then echo "FAIL: game or personal files in package:"; echo "$leaks"; fail=1; fi
# 2. Every library resolves: from the package or the system, never this machine's build tree.
deps="$(cd "$stage" && LD_LIBRARY_PATH= ldd ./veronicax ./librexgpu-xenos.so)"
if grep -q 'not found' <<< "$deps"; then echo "FAIL: unresolved libraries:"; grep 'not found' <<< "$deps"; fail=1; fi
if grep -E "=> ($root|/home)" <<< "$deps" | grep -v "$stage" | grep -q .; then
    echo "FAIL: libraries resolved from outside the package:"; grep -E "=> ($root|/home)" <<< "$deps" | grep -v "$stage"; fail=1
fi
# 3. Stay runnable on older distributions: the SDK libraries need glibc 2.35 /
#    libstdc++ 3.4.32 (Ubuntu 24.04, Debian 13); the game must not raise that.
glibc="$(objdump -T "$stage"/veronicax "$stage"/*.so | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1)"
glibcxx="$(objdump -T "$stage"/veronicax "$stage"/*.so | grep -oE 'GLIBCXX_[0-9.]+' | sort -Vu | tail -1)"
echo "   needs $glibc and $glibcxx (libstdc++) or newer"
max_glibc=2.35 max_glibcxx=3.4.32
if [ "$(printf '%s\n' "GLIBC_$max_glibc" "$glibc" | sort -V | tail -1)" != "GLIBC_$max_glibc" ] ||
   [ "$(printf '%s\n' "GLIBCXX_$max_glibcxx" "$glibcxx" | sort -V | tail -1)" != "GLIBCXX_$max_glibcxx" ]; then
    echo "FAIL: needs newer than glibc $max_glibc / libstdc++ $max_glibcxx; symbols responsible:"
    objdump -T "$stage"/veronicax | grep -E "($glibc|$glibcxx)\b" | awk '{print "     ", $(NF-1), $NF}' | sort -u | head
    fail=1
fi
[ "$fail" = 0 ] || { echo "Package check failed; not archiving." >&2; exit 1; }

echo "== Archiving"
tar -C dist -czf "dist/$name.tar.gz" "$name"
(cd dist && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
du -sh "$stage" "dist/$name.tar.gz" | sed 's/^/   /'
echo "Done: dist/$name.tar.gz"
