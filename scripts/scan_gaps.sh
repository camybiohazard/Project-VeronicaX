#!/usr/bin/env bash
# Repeat codegen + refine_gaps.py until no new pointer-only functions turn up.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

for pass in $(seq 1 "${1:-10}"); do
    echo "== pass $pass"
    ok=1
    scripts/codegen.sh > /dev/null 2>&1 || ok=0
    out="$(python3 -I scripts/refine_gaps.py)"
    echo "$out"
    case "$out" in
        *"(+0 this pass), added 0")
            if [ $ok = 1 ]; then exit 0; fi
            # Validation failed on something refine_gaps.py does not handle.
            tail -n 5 notes/logs/codegen.stdout
            exit 1 ;;
    esac
done
echo "not converged after $pass passes; run again" >&2
exit 1
