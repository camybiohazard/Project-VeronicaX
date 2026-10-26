#!/usr/bin/env bash
# Run the recompiled game. Usage: scripts/run.sh [preset] [extra rexglue flags...]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

preset="${1:-dev-relwithdebinfo}"
shift || true

bin="$(find "out/build/$preset" -maxdepth 3 -type f -name veronicax -perm -u+x | head -n 1)"
if [ -z "$bin" ]; then
    echo "veronicax binary not found under out/build/$preset; run scripts/build.sh first" >&2
    exit 1
fi

mkdir -p notes/logs
exec "$bin" --game_data_root="$root/game/extracted" --log_file="$root/notes/logs/run.log" "$@"
