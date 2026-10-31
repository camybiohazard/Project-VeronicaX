#!/usr/bin/env bash
# Run a Windows package under Proton on this machine, without Steam open.
# Usage: scripts/run-proton.sh [package folder] [extra rexglue flags...]
#   package folder defaults to the newest dist/VeronicaX-*-windows-x86_64.
# The Wine prefix (and so the Windows saves, under
# drive_c/users/steamuser/Documents/veronicax) is $VERONICAX_PROTON_PREFIX,
# default ~/.local/share/veronicax-proton. $PROTON picks a Proton other than
# Steam's Proton Experimental. The launcher asks for the game file on first
# run: Linux files are on Z:, e.g. Z:\home\<you>\...\game\CodeVeronica.iso.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

if [ $# -gt 0 ] && [ -d "$1" ]; then
    package="$(cd "$1" && pwd)"
    shift
else
    package="$(ls -1dt dist/VeronicaX-*-windows-x86_64/ 2> /dev/null | head -n 1 || true)"
    [ -n "$package" ] || { echo "No dist/VeronicaX-*-windows-x86_64 package; run scripts/package-windows.sh" >&2; exit 1; }
    package="$root/${package%/}"
fi
[ -f "$package/veronicax.exe" ] || { echo "$package/veronicax.exe not found" >&2; exit 1; }

steam=""
for dir in ~/.steam/steam ~/.steam/debian-installation ~/.local/share/Steam; do
    [ -d "$dir/steamapps" ] && { steam="$(cd "$dir" && pwd -P)"; break; }
done
proton="${PROTON:-$steam/steamapps/common/Proton - Experimental/proton}"
[ -n "$steam" ] && [ -x "$proton" ] ||
    { echo "Proton not found (install Proton Experimental in Steam, or set PROTON)" >&2; exit 1; }

prefix="${VERONICAX_PROTON_PREFIX:-$HOME/.local/share/veronicax-proton}"
mkdir -p "$prefix"

echo "Running $package/veronicax.exe"
echo "Logs: $package/logs/   Prefix: $prefix"
cd "$package"
STEAM_COMPAT_DATA_PATH="$prefix" STEAM_COMPAT_CLIENT_INSTALL_PATH="$steam" \
    exec "$proton" run ./veronicax.exe "$@"
