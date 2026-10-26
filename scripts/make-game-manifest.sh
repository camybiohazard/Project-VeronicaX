#!/usr/bin/env bash
# Write build-win/game-manifest.txt (SHA-256, size and path of every file in
# game/extracted) and build-win/verify-game.ps1, which checks a copied "game"
# folder on Windows against it. The manifest describes the game files, so
# keep it with your own copy only: it is not part of a release.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/game/extracted"
out="$root/build-win"
mkdir -p "$out"

(cd "$src" && find . -type f -print0 | sort -z | while IFS= read -r -d '' f; do
    rel="${f#./}"
    printf '%s %s %s\n' "$(sha256sum "$f" | cut -d' ' -f1)" "$(stat -c %s "$f")" "${rel//\//\\}"
done) > "$out/game-manifest.txt"

cat > "$out/verify-game.ps1" <<'EOF'
# Checks the "game" folder next to this script against game-manifest.txt.
# Run: powershell -ExecutionPolicy Bypass -File verify-game.ps1
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$game = Join-Path $here 'game'
$bad = 0
foreach ($line in Get-Content (Join-Path $here 'game-manifest.txt')) {
    $hash, $size, $rel = $line -split ' ', 3
    $path = Join-Path $game $rel
    if (-not (Test-Path -LiteralPath $path)) { Write-Host "MISSING  $rel"; $bad++; continue }
    $item = Get-Item -LiteralPath $path
    if ($item.Length -ne [int64]$size) { Write-Host "SIZE     $rel ($($item.Length), expected $size)"; $bad++; continue }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()
    if ($actual -ne $hash) { Write-Host "CHANGED  $rel"; $bad++ }
}
if ($bad -eq 0) { Write-Host 'All game files match.' } else { Write-Host "$bad file(s) missing or damaged." }
EOF

echo "Wrote $(wc -l < "$out/game-manifest.txt") entries to build-win/game-manifest.txt and build-win/verify-game.ps1"
