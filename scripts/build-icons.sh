#!/bin/bash
# Encode the approved PNG into native icon containers; no artwork generation.
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd)"
icon_root="$project_root/assets/icons"
work="$(mktemp -d "${TMPDIR:-/tmp}/dnd-icons.XXXXXX")"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/app.iconset"
for size in 16 32 128 256 512; do
  sips --resampleHeightWidth "$size" "$size" "$icon_root/app-icon.png" --out "$work/app.iconset/icon_${size}x${size}.png" >/dev/null
  double=$((size * 2))
  sips --resampleHeightWidth "$double" "$double" "$icon_root/app-icon.png" --out "$work/app.iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil --convert icns --output "$icon_root/app-icon.icns" "$work/app.iconset"
for size in 16 24 32 48 64 128 256; do
  sips --resampleHeightWidth "$size" "$size" "$icon_root/app-icon.png" --out "$work/windows-$size.png" >/dev/null
done
node "$project_root/scripts/encode-ico.mjs" "$work" "$icon_root/app-icon.ico"
printf 'Encoded macOS and Windows icons in %s\n' "$icon_root"
