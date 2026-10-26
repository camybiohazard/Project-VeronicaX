#!/usr/bin/env bash
# Extract the game ISO into game/extracted (needed once, or after a new dump).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
iso="${1:-$root/game/CodeVeronica.iso}"

mkdir -p "$root/game/extracted" "$root/notes"
python3 -I "$root/scripts/xiso_extract.py" "$iso" "$root/game/extracted" > "$root/notes/iso_listing.txt"
echo "Extracted $(grep -c '^f' "$root/notes/iso_listing.txt") files to game/extracted"
