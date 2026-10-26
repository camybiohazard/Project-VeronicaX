#!/usr/bin/env bash
# Boot the game for 30 s without input and check it reaches the storage
# device selector (about 15 s into a good boot) with no fatal errors.
# Run after every codegen change. Opens the game window briefly.
# Usage: scripts/smoke-test.sh [preset]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

bin="out/build/${1:-dev-relwithdebinfo}/veronicax"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

# Wayland without libdecor: libdecor's GTK plugin can abort outside a full
# desktop session, and X11 takes a different (crashing) boot path.
SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=0 timeout -s INT 30 "$bin" --game_data_root="$root/game/extracted" \
    --log_file="$log" --log_level=debug > /dev/null 2>&1 || true

fatal="$(grep -E '\[critical\]|FATAL|Unhandled guest access violation|Switch case out of range' "$log" | head -n 3 || true)"
if [ -n "$fatal" ]; then
    echo "FAIL: fatal error during boot:"
    echo "$fatal"
    exit 1
fi
if ! grep -q 'XamShowDeviceSelectorUI' "$log"; then
    echo "FAIL: never reached the device selector (stalled during boot)"
    grep -vE 'XMA: Write|PRESENT|XGIUserSetContextEx|AudioWorker|SubmitFrame|SDLCallback' "$log" | tail -n 5
    exit 1
fi
echo "PASS: reached the device selector with no fatal errors"
