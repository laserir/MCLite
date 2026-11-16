#!/usr/bin/env bash
# Regenerate the monochrome emoji fonts in firmware/src/ui/fonts/.
# Output .c files are checked in; the source .ttf is NOT (1.5 MB) — fetch it first:
#
#   OpenMoji-black-glyf.ttf  (CC-BY-SA 4.0)
#   https://github.com/hfg-gmuend/openmoji/releases  (the "black" / glyf build)
#
# Requires Node + lv_font_conv:  npm i -g lv_font_conv
#
# Sizes match theme.h: T-Deck uses 12 (body) + 14 (heading); T-Watch uses 16 + 20.
# 1 bpp (monochrome) — LVGL colour emoji is heavy/complex and not used here.
# --lv-fallback wires the ext font of the same size (accented Latin + Cyrillic, see
# generate_ext_fonts.sh) into each font's .fallback, and that one falls back to the
# built-in Montserrat, so ASCII renders identically and only emoji come from here.
set -e
cd "$(dirname "$0")"

FONT="OpenMoji-black-glyf.ttf"
RANGES=(-r 0x2600-0x26FF -r 0x2700-0x27BF -r 0x1F300-0x1F6FF -r 0x1F900-0x1FAFF)
OUT=../../src/ui/fonts

for SIZE in 12 14 16 20; do
  npx lv_font_conv \
    --font "$FONT" "${RANGES[@]}" \
    --size "$SIZE" --bpp 1 --format lvgl \
    --lv-font-name "lv_font_emoji_${SIZE}" \
    --lv-fallback "lv_font_ext_${SIZE}" \
    -o "${OUT}/emoji_font_${SIZE}.c"
done
echo "Done. Re-add the per-board #ifdef guards (PLATFORM_TDECK for 12/14, PLATFORM_TWATCH for 16/20)."
