#!/usr/bin/env bash
# Dump the loaded XEX image (0x82000000-0x82EE0000) from a running build to
# game/image.bin. refine_gaps.py uses it to find addresses that vtables,
# function-pointer tables or code refer to. Needs a built binary; rerun only
# if the XEX changes.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

bin="out/build/${1:-dev-relwithdebinfo}/veronicax"
out="$root/game/image.bin"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

# Let the game boot for a few seconds so the image is loaded, then dump it.
timeout -s INT 10 gdb -q -batch \
    -ex "set debuginfod enabled off" \
    -ex "handle SIGSEGV SIGBUS SIGPIPE SIG32 SIG33 SIG34 SIG35 SIG36 nostop noprint pass" \
    -ex "run" \
    -ex "dump binary memory $out 0x182000000 0x182EE0000" \
    -ex "kill" \
    --args "$bin" --game_data_root="$root/game/extracted" --log_file="$log" \
    > /dev/null 2>&1 || true

[ -s "$out" ] || { echo "image dump failed" >&2; exit 1; }
echo "Wrote $(stat -c %s "$out") bytes to game/image.bin"
