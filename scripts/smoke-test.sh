#!/usr/bin/env bash
# Boot the game without input and check it reaches the storage device
# selector with no fatal errors. Stops as soon as it gets there (about 15 s
# into a good boot) and fails only after [limit] seconds (default 90): some
# boots on this machine take over a minute (notes/known-issues.md, Testing).
# Run after every codegen change. Opens the game window briefly.
# In a terminal it waits 10 s before exiting, so a window that closes with the
# script still shows the result; SMOKE_NO_PAUSE=1 skips that.
# Usage: scripts/smoke-test.sh [preset] [limit]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

bin="out/build/${1:-dev-relwithdebinfo}/veronicax"
limit="${2:-90}"
log="$(mktemp)"
finish() {
    rm -f "$log"
    if [ -t 1 ] && [ "${SMOKE_NO_PAUSE:-0}" != 1 ]; then
        echo "Closing in 10 s..."
        sleep 10
    fi
}
trap finish EXIT
fatal_re='\[critical\]|FATAL|Unhandled guest access violation|Switch case out of range'

# Wayland without libdecor: libdecor's GTK plugin can abort outside a full
# desktop session, and X11 takes a different (crashing) boot path.
SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=0 "$bin" --game_data_root="$root/game/extracted" \
    --log_file="$log" --log_level=debug --veronicax_show_launcher=false > /dev/null 2>&1 &
pid=$!
start=$SECONDS

# Poll the log until the selector, a fatal error, the game exiting, or the limit.
result=timeout
while [ $((SECONDS - start)) -lt "$limit" ]; do
    if grep -qE "$fatal_re" "$log"; then result=fatal; break; fi
    if grep -q 'XamShowDeviceSelectorUI' "$log"; then result=pass; break; fi
    if ! kill -0 "$pid" 2> /dev/null; then result=exited; break; fi
    sleep 0.5
done
elapsed=$((SECONDS - start))

# SIGINT closes the window normally; kill it if it hasn't gone after 10 s.
kill -INT "$pid" 2> /dev/null || true
for _ in $(seq 20); do kill -0 "$pid" 2> /dev/null || break; sleep 0.5; done
kill -KILL "$pid" 2> /dev/null || true
wait "$pid" 2> /dev/null || true

case "$result" in
    pass)
        echo "PASS: reached the device selector after ${elapsed} s with no fatal errors" ;;
    fatal)
        echo "FAIL: fatal error during boot (after ${elapsed} s):"
        grep -E "$fatal_re" "$log" | head -n 3
        exit 1 ;;
    exited)
        echo "FAIL: the game exited after ${elapsed} s without reaching the device selector:"
        tail -n 5 "$log"
        exit 1 ;;
    timeout)
        echo "FAIL: never reached the device selector within ${limit} s (stalled during boot)"
        grep -vE 'XMA: Write|PRESENT|XGIUserSetContextEx|AudioWorker|SubmitFrame|SDLCallback|Completed delivery of APC' "$log" | tail -n 5
        echo "If this repeats, run scripts/debug-run.sh and press Ctrl+C once it hangs."
        exit 1 ;;
esac
