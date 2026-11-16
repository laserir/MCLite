#!/usr/bin/env bash
# Regenerate the extended-character Montserrat fonts in firmware/src/ui/fonts/ext_font_*.c
# (issue #53: Cyrillic + accented Latin).
#
# LVGL's built-in lv_font_montserrat_N only carries ASCII (+ a few symbols). These fonts
# add Latin-1 Supplement + Latin Extended-A (U+00A0-017F), Cyrillic (U+0400-04FF) and
# the numero sign (U+2116), generated from the SAME Montserrat-Medium.ttf LVGL builds its
# built-ins from, at the same 4 bpp, so mixed text looks seamless. Each font's .fallback is
# the built-in of the same size, so ASCII and LV_SYMBOL_* glyphs still come from there.
#
# Fallback chains (see theme.h):
#   FONT_BODY/HEADING:  [colour imgfont ->] emoji_N -> ext_N -> montserrat_N
#   FONT_SMALL..TITLE:  ext_N -> montserrat_N
#
# Metrics: a label lays out with its PRIMARY font's line_height/base_line. The ext fonts
# are primary for FONT_SMALL..TITLE, and lv_font_conv would derive taller metrics from the
# accented capitals (e.g. 19/4 instead of 16/3 at 14 px), shifting every screen. So the
# generated line_height/base_line are overwritten with the built-in's values. Accents on
# capitals (and Latvian cedillas) can then exceed the line box by 1-2 px; the script prints
# how many glyphs do, per size.
#
# Sizes: T-Deck uses 12/14/16/20, T-Watch 14/16/20/24 (theme.h). 12 is gated to
# PLATFORM_TDECK and 24 to PLATFORM_TWATCH to save flash; 14/16/20 are shared.
#
# Requires Node + lv_font_conv (npx fetches it) and a prior `pio run` so the LVGL library
# (which ships Montserrat-Medium.ttf, SIL OFL 1.1) is present under .pio/libdeps.
set -euo pipefail
cd "$(dirname "$0")"

FW=../..
OUT=$FW/src/ui/fonts
LVGL=""
for env in tdeck_plus twatch_ultra; do
  if [ -f "$FW/.pio/libdeps/$env/lvgl/scripts/built_in_font/Montserrat-Medium.ttf" ]; then
    LVGL="$FW/.pio/libdeps/$env/lvgl"; break
  fi
done
if [ -z "$LVGL" ]; then
  echo "ERROR: LVGL not found under firmware/.pio/libdeps. Run 'pio run -d firmware -e tdeck_plus' once first." >&2
  exit 1
fi
TTF="$LVGL/scripts/built_in_font/Montserrat-Medium.ttf"
RANGES=(-r 0x00A0-0x017F -r 0x0400-0x04FF -r 0x2116)

for SIZE in 12 14 16 20 24; do
  BUILTIN="$LVGL/src/font/lv_font_montserrat_${SIZE}.c"
  LH=$(sed -n 's/^ *\.line_height = \([0-9]*\),.*/\1/p' "$BUILTIN")
  BL=$(sed -n 's/^ *\.base_line = \([0-9]*\),.*/\1/p' "$BUILTIN")
  [ -n "$LH" ] && [ -n "$BL" ] || { echo "ERROR: could not read metrics from $BUILTIN" >&2; exit 1; }

  case $SIZE in
    12) GUARD=PLATFORM_TDECK ;;
    24) GUARD=PLATFORM_TWATCH ;;
    *)  GUARD="" ;;
  esac

  TMP=$(mktemp)
  npx --yes lv_font_conv \
    --font "$TTF" "${RANGES[@]}" \
    --size "$SIZE" --bpp 4 --no-compress --format lvgl \
    --lv-font-name "lv_font_ext_${SIZE}" \
    --lv-fallback "lv_font_montserrat_${SIZE}" \
    -o "$TMP"

  # Pin the metrics to the built-in's (see header).
  sed -i -e "s/^\( *\.line_height = \)[0-9]*,/\1${LH},/" \
         -e "s/^\( *\.base_line = \)[0-9]*,/\1${BL},/" "$TMP"

  # Report glyphs that overflow the pinned line box: top above (LH-BL), bottom below -BL.
  awk -v top=$((LH - BL)) -v bot=$((-BL)) -v size=$SIZE '
    /\.box_h = / {
      match($0, /\.box_h = [0-9]+/);  h = substr($0, RSTART + 9, RLENGTH - 9) + 0
      match($0, /\.ofs_y = -?[0-9]+/); y = substr($0, RSTART + 9, RLENGTH - 9) + 0
      if (h > 0 && (h + y > top || y < bot)) n++
    }
    END { printf "  %2d px: line %d / base %d, %d glyph(s) exceed the line box\n", size, top + (-bot), -bot, n }
  ' "$TMP"

  {
    echo "// Montserrat Medium (SIL OFL 1.1, (c) The Montserrat Project Authors): accented Latin + Cyrillic,"
    echo "// generated via lv_font_conv. Falls back to LVGL's built-in lv_font_montserrat_${SIZE} for ASCII/symbols."
    echo "// line_height/base_line pinned to the built-in's. See assets/fonts/generate_ext_fonts.sh."
    if [ -n "$GUARD" ]; then
      echo "// Gated to the only board that uses this size, to save flash."
      echo "#ifdef ${GUARD}"
    fi
    # Drop lv_font_conv's absolute temp/LVGL paths from the Opts: line so output is reproducible.
    sed -e "s|--font [^ ]*Montserrat-Medium.ttf|--font Montserrat-Medium.ttf|" -e "s| -o /[^ ]*| -o ext_font_${SIZE}.c|" "$TMP"
    if [ -n "$GUARD" ]; then echo "#endif  // ${GUARD}"; fi
  } > "$OUT/ext_font_${SIZE}.c"
  rm -f "$TMP"
done
echo "Done."
