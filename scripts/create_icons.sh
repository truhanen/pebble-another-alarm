#!/bin/bash
# Rasterize an SVG into the icon sizes Pebble app resources need (25x25,
# 80x80, 144x144), then quantize the result onto Pebble's platform palette:
# RGB is remapped onto the 64-color palette (scripts/pebble_colors_64.gif,
# a copy of the indexed reference image from the Pebble iconography repo —
# see journal.md's "2025-11-30 App icon" entry) and alpha is posterized to
# Pebble's 2-bit (4-level) alpha channel. Requires ImageMagick (`magick`).
# Source SVG: scripts/icon.svg. The 25x25 menu icon goes to
# resources/images/; the 80x80/144x144 store icons go to appstore/.
#
# The 25x25 menu icon keeps its transparency (it's composited over the
# system menu's own background on-watch). The 80x80/144x144 app-store icons
# are flattened onto an opaque white background instead — those are shown
# on their own (store listing, etc.), so a transparent background there
# would let whatever's behind them show through.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PALETTE="$SCRIPT_DIR/pebble_colors_64.gif"

REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SVG="${1:-$SCRIPT_DIR/icon.svg}"

if [ ! -f "$SVG" ]; then
  echo "SVG file not found: $SVG" >&2
  exit 1
fi

STORE_OUTDIR="$REPO_ROOT/appstore"
MENU_ICON_OUT="$REPO_ROOT/resources/images/menuicon.png"
mkdir -p "$STORE_OUTDIR"

for size in 25 80 144; do
  if [ "$size" -eq 25 ]; then
    OUT="$MENU_ICON_OUT"
  else
    OUT="$STORE_OUTDIR/icon_${size}x${size}.png"
  fi
  RAW="$(mktemp -t icon_raw).png"
  MASK="$(mktemp -t icon_mask).png"
  RGB="$(mktemp -t icon_rgb).png"

  if [ "$size" -eq 25 ]; then
    sips --resampleHeightWidth "$size" "$size" -s format png "$SVG" -o "$RAW" >/dev/null
    # Pebble's alpha channel is 2-bit (4 levels: 0/85/170/255).
    magick "$RAW" -channel A -posterize 4 +channel -alpha extract "$MASK"
    # Remap RGB onto the 64-color platform palette, ignoring alpha for the match.
    magick "$RAW" -alpha off -dither None -remap "$PALETTE" "$RGB"
    # Recombine the quantized RGB with the quantized alpha mask.
    magick "$RGB" "$MASK" -alpha off -compose CopyOpacity -composite "$OUT"
  else
    # Rasterize very slightly smaller than the full canvas, uniformly on
    # both axes (so the symbol's aspect ratio is preserved, not stretched),
    # then center it back onto the full-size canvas below -- that leaves a
    # small white margin on every side, including above/below.
    pad=$((size / 20))
    if [ "$pad" -lt 1 ]; then pad=1; fi
    inner=$((size - 2 * pad))
    sips --resampleHeightWidth "$inner" "$inner" -s format png "$SVG" -o "$RAW" >/dev/null
    # Flatten onto opaque white, pad back out to the full square, then
    # quantize -- no alpha mask needed since the result has no transparency
    # left. White (255,255,255) is itself one of the 64 palette colors, so
    # it survives the remap exactly.
    magick "$RAW" -background white -alpha remove -alpha off \
      -gravity center -extent "${size}x${size}" \
      -dither None -remap "$PALETTE" "$OUT"
  fi

  rm -f "$RAW" "$MASK" "$RGB"
  echo "Wrote $OUT"
done
