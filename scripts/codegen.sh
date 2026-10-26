#!/usr/bin/env bash
# Run analysis + C++ generation, keeping the log in notes/logs/codegen.stdout.
# Extra args go to rexglue (e.g. --ignore-stamp).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

mkdir -p notes/logs
log=notes/logs/codegen.stdout
status=0
./sdk/bin/rexglue codegen "$@" veronicax_manifest.toml > "$log" 2>&1 || status=$?
tail -n 5 "$log"

# Summarise unresolved branch targets: these become entries in config/functions.toml.
if grep -q 'target not in any function' "$log"; then
    echo
    echo "Unresolved targets (count  address):"
    grep 'target not in any function' "$log" | awk '{print $1}' | sort | uniq -c | sort -rn
fi
exit $status
