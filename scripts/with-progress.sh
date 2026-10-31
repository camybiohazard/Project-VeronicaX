#!/usr/bin/env bash
# Run a build command quietly, but show ninja's [n/total] progress so a long
# full rebuild doesn't look stalled. The full output goes to <log>; on failure
# its last lines are printed.
# Usage: scripts/with-progress.sh <log> <command> [args...]
set -euo pipefail
log="$1"
shift
mkdir -p "$(dirname "$log")"

status=0
"$@" > "$log" 2>&1 &
pid=$!
last=""
while kill -0 "$pid" 2> /dev/null; do
    # -a: compiler output has colour codes and UTF-8 quotes, which would
    # otherwise make grep treat the log as binary.
    step="$(grep -aoE '^\[[0-9]+/[0-9]+\]' "$log" | tail -n 1 || true)"
    if [ -n "$step" ] && [ "$step" != "$last" ]; then
        if [ -t 1 ]; then printf '\r   %s compiling (log: %s)' "$step" "$log"; else echo "   $step"; fi
        last="$step"
    fi
    sleep 2
done
wait "$pid" || status=$?
[ -t 1 ] && [ -n "$last" ] && echo
if [ "$status" != 0 ]; then
    echo "Build failed (exit $status); last lines of $log:" >&2
    tail -n 20 "$log" >&2
fi
exit "$status"
