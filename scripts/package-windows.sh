#!/usr/bin/env bash
# Cross-build the Windows Release and assemble a ready-to-share folder plus .zip
# in dist/:
#
#   dist/VeronicaX-<version>-windows-x86_64/
#     veronicax.exe           the game (opens on the launcher)
#     rexruntime.dll          ReXGlue runtime
#     rexgpu-xenos.dll        GPU plugin (D3D12)
#     README.txt              player instructions
#     LICENSE                 this project's licence
#     third_party_licenses/   ReXGlue + bundled libraries (scripts/fetch-licenses.sh)
#
# No game data and no Microsoft SDK/CRT files are ever included. Players need
# the Visual C++ 2015-2022 x64 redistributable, as most PCs already have.
# Usage: scripts/package-windows.sh [version] [jobs]
#   version defaults to 0.1.0-<git short hash>[-dirty]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

version="${1:-0.2.0-$(git describe --always --dirty)}"
jobs="${2:-4}"
build="build-win"
name="VeronicaX-$version-windows-x86_64"
stage="dist/$name"
readobj=llvm-readobj-20

[ -d third_party_licenses ] || { echo "third_party_licenses/ missing: run scripts/fetch-licenses.sh" >&2; exit 1; }
[ -f LICENSE ] || { echo "LICENSE missing" >&2; exit 1; }

echo "== Building win-cross-release"
scripts/with-progress.sh notes/logs/package-windows-build.log scripts/build-win.sh "$jobs"

echo "== Staging $stage"
rm -rf "$stage" "dist/$name.zip" "dist/$name.zip.sha256"
mkdir -p "$stage"
cp "$build/veronicax.exe" "$build/rexruntime.dll" "$build/rexgpu-xenos.dll" "$stage/"
cp -r third_party_licenses "$stage/"
cp LICENSE "$stage/LICENSE"

# CRLF so Notepad on older Windows shows it properly.
sed 's/$/\r/' > "$stage/README.txt" <<EOF
VeronicaX $version - Resident Evil CODE: Veronica X (Xbox 360), recompiled
for Windows x64.

This package contains no game data. You need your own copy of Resident Evil
CODE: Veronica X for Xbox 360: a disc image (ISO) or the Games on Demand
version.

Getting started
  1. Extract the whole zip to a normal folder you can write to, such as
     Documents or a games folder. Don't run it from inside the zip or from
     Program Files. You need about 2 GB of free space.
  2. Run veronicax.exe. Windows may warn "Windows protected your PC" because
     the program is not signed: click "More info", then "Run anyway".
  3. On first launch, click "Select game file..." and choose your ISO. For
     the Games on Demand version, choose the file with the long hex name in
     43430822\\00007000 (keep its .data folder next to it). The launcher
     checks it is the supported version and extracts it into a "game" folder
     here. Don't keep the ISO or Games on Demand files inside that folder.
  4. Pick your display settings and press Play.

Later launches go straight to the settings screen; untick "Show this launcher
at startup" to skip it. Press F4 in game for more options.

Files created next to the program
  game\\            extracted game files
  veronicax.toml   your settings
  logs\\            log files: include the newest one when reporting a problem

Saves and profile
  Saves are kept outside this folder, so they survive updates and
  re-extracting the game:
    Documents\\veronicax\\
      B13EBABEBABEBABE\\43430822\\    your saves (copy this to back them up)
      achievements\\                 unlocked achievements
      cache\\                        shader cache (safe to delete)
  To start over, delete B13EBABEBABEBABE and achievements.

Requirements
  - Windows 10 or 11, 64-bit
  - A Direct3D 12 capable GPU with an up-to-date driver
  - Microsoft Visual C++ 2015-2022 Redistributable (x64). If the game says
    MSVCP140.dll or VCRUNTIME140.dll is missing, install it from
    https://aka.ms/vs/17/release/vc_redist.x64.exe

Built with the ReXGlue SDK (https://github.com/rexglue/rexglue-sdk), which
includes code derived from Xenia (https://xenia.jp). See third_party_licenses\\.
EOF

echo "== Checking the package"
fail=0
# 1. Nothing game-derived, personal, or Microsoft SDK/CRT.
leaks="$(find "$stage" -path "$stage/third_party_licenses" -prune -o \( -iname '*.xex' -o -iname '*.iso' -o -iname 'image.bin' -o -iname '*.toml' \
    -o -iname '*.log' -o -iname 'game-manifest.txt' -o -iname 'verify-game.ps1' -o -iname '*.bat' \
    -o -iname '*.lib' -o -iname '*.pdb' -o -iname 'vcruntime*' -o -iname 'msvcp*' \
    -o -name game -o -name logs \) -print)"
if [ -n "$leaks" ]; then echo "FAIL: unexpected files in package:"; echo "$leaks"; fail=1; fi
# 2. Every DLL the binaries import is bundled, part of Windows, or part of the
#    VC++ redistributable.
allowed='^(rexruntime\.dll|kernel32\.dll|user32\.dll|gdi32\.dll|shell32\.dll|ole32\.dll|oleaut32\.dll|advapi32\.dll|comdlg32\.dll|imm32\.dll|setupapi\.dll|version\.dll|winmm\.dll|ws2_32\.dll|bcrypt\.dll|dxgi\.dll|d3d12\.dll|api-ms-win-.*\.dll|vcruntime140(_1)?\.dll|msvcp140(_atomic_wait|_1|_2)?\.dll)$'
for bin in veronicax.exe rexruntime.dll rexgpu-xenos.dll; do
    for dll in $($readobj --coff-imports "$stage/$bin" | awk '/^ *Name:/ {print tolower($2)}' | sort -u); do
        if ! grep -qE "$allowed" <<< "$dll"; then
            echo "FAIL: $bin imports $dll, which players won't have"; fail=1
        fi
    done
done
# 3. All three must be 64-bit Windows binaries (a truncated or wrong file is
#    what made Windows say "This app can't run on your PC").
for bin in veronicax.exe rexruntime.dll rexgpu-xenos.dll; do
    $readobj --file-headers "$stage/$bin" | grep -q 'IMAGE_FILE_MACHINE_AMD64' ||
        { echo "FAIL: $bin is not an x64 Windows binary"; fail=1; }
done
[ "$fail" = 0 ] || { echo "Package check failed; not archiving." >&2; exit 1; }

echo "== Archiving"
(cd dist && zip -qr -9 "$name.zip" "$name" && sha256sum "$name.zip" > "$name.zip.sha256")
du -sh "$stage" "dist/$name.zip" | sed 's/^/   /'
echo "Done: dist/$name.zip"
