#!/usr/bin/env python3
"""Bake the on-device-selectable emoji to colour images for LVGL's lv_imgfont.

Companion to generate_emoji_fonts.sh, which builds the *monochrome* OpenMoji
fonts covering ~1300 glyphs. This covers only the ~34 emoji a user can actually
pick on the device (the chat picker + the reaction set); everything else keeps
falling back to those mono fonts, so nothing renders blank.

  source : OpenMoji colour PNGs (CC-BY-SA 4.0) — same project as the mono font,
           so the existing attribution in LICENSES.md still covers this.
  format : LV_IMG_CF_TRUE_COLOR_ALPHA, RGB565 + 8-bit alpha. Byte order follows
           LV_COLOR_16_SWAP, which is READ OUT OF firmware/lv_conf.h rather than
           assumed -- this build sets it to 1, so pixels are [hi, lo, alpha].
           Getting this backwards compiles and runs fine but renders the colours
           mangled, so the value is parsed, not hardcoded.
  sizes  : T-Deck 12/14 px, T-Watch 16/20 px — matching FONT_BODY / FONT_HEADING
           in theme.h. imgfont glyphs do NOT scale with the text, so each size
           needs its own baked set.

Glyphs are autocropped and bottom-aligned so they sit on the text baseline.

Usage:  python3 generate_color_emoji.py            # regenerate all sizes
        python3 generate_color_emoji.py --check    # verify checked-in output is current
"""
import io, os, re, sys, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "src", "ui", "fonts"))
CHAT = os.path.normpath(os.path.join(HERE, "..", "..", "src", "ui", "ChatScreen.cpp"))
CACHE = os.path.join(HERE, ".openmoji-cache")
LV_CONF = os.path.normpath(os.path.join(HERE, "..", "..", "lv_conf.h"))
URL = "https://raw.githubusercontent.com/hfg-gmuend/openmoji/master/color/72x72/{cp}.png"

# size -> the platform whose FONT_BODY/FONT_HEADING use it (see theme.h)
SIZES = {12: "PLATFORM_TDECK", 14: "PLATFORM_TDECK", 16: "PLATFORM_TWATCH", 20: "PLATFORM_TWATCH"}


def codepoints_from_source():
    """Single source of truth: the picker and reaction maps in ChatScreen.cpp.

    Reading them here rather than keeping a second list is deliberate -- wadamesh
    shipped tofu twice because their picker list and their baked glyph set drifted
    apart. If a glyph is offered on-device it gets baked, by construction.
    """
    src = io.open(CHAT, encoding="utf-8").read()
    out = set()
    for name in ("emojiMap", "reactMap"):
        blk = src[src.index(f"static const char* {name}[]"):]
        blk = blk[:blk.index("};")]
        for esc in re.findall(r'"((?:\\x[0-9A-Fa-f]{2})+)"', blk):
            raw = bytes(int(x, 16) for x in re.findall(r"\\x([0-9A-Fa-f]{2})", esc))
            try:
                out.add(ord(raw.decode("utf-8")))
            except (UnicodeDecodeError, TypeError):
                # A multi-codepoint sequence (an emoji carrying U+FE0F, a skin-tone
                # modifier, a ZWJ pair) cannot be baked as a single glyph. Say so
                # rather than dropping it silently: it falls back to the mono font
                # with nothing explaining why, and --check would still report "in
                # sync" because the baked set matches this same filtered list.
                print(f"  note: {esc} is multi-codepoint, not bakeable - stays monochrome")
    return sorted(out)


def color_16_swap():
    """LV_COLOR_16_SWAP from lv_conf.h. Decides RGB565 byte order in the output."""
    src = io.open(LV_CONF, encoding="utf-8").read()
    m = re.search(r"^\s*#define\s+LV_COLOR_16_SWAP\s+(\d+)", src, re.M)
    if not m:
        sys.exit("could not read LV_COLOR_16_SWAP from " + LV_CONF)
    d = re.search(r"^\s*#define\s+LV_COLOR_DEPTH\s+(\d+)", src, re.M)
    if not d or d.group(1) != "16":
        sys.exit("this generator only emits RGB565; LV_COLOR_DEPTH is not 16")
    return int(m.group(1))


SWAP = None   # resolved lazily in bake(); --check never bakes


def fetch(cp):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, f"{cp:04X}.png")
    if not os.path.exists(path):
        with urllib.request.urlopen(URL.format(cp=f"{cp:04X}"), timeout=30) as r:
            data = r.read()
        io.open(path, "wb").write(data)
    return path


def bake(png_path, px):
    """Autocrop, scale to fit, bottom-align, emit RGB565 + alpha per pixel."""
    swap = color_16_swap()
    from PIL import Image
    img = Image.open(png_path).convert("RGBA")
    bbox = img.split()[3].getbbox()          # trim transparent margin
    if bbox:
        img = img.crop(bbox)
    w, h = img.size
    scale = min(px / w, px / h)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    content = img.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    canvas.paste(content, ((px - nw) // 2, px - nh))   # bottom-align to baseline
    out = bytearray()
    for (r, g, b, a) in list(canvas.getdata()):
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        hi, lo = (v >> 8) & 0xFF, v & 0xFF
        out += bytes((hi, lo, a) if swap else (lo, hi, a))
    return bytes(out)


def render(px, cps):
    guard = SIZES[px]
    L = []
    L.append("// AUTO-GENERATED by firmware/assets/fonts/generate_color_emoji.py")
    L.append("// OpenMoji colour (CC-BY-SA 4.0). Do not edit by hand -- rerun the script.")
    L.append(f"// {len(cps)} glyphs baked to RGB565+alpha at {px}px for LVGL lv_imgfont.")
    L.append("#include <lvgl.h>")
    L.append(f"#ifdef {guard}")
    L.append("")
    for cp in cps:
        data = bake(fetch(cp), px)
        rows = [", ".join(str(b) for b in data[i:i + 24]) for i in range(0, len(data), 24)]
        L.append(f"static const uint8_t ce{px}_{cp:04x}[] = {{")
        L.append("    " + ",\n    ".join(rows))
        L.append("};")
        L.append(f"static const lv_img_dsc_t cd{px}_{cp:04x} = {{")
        L.append(f"    {{ LV_IMG_CF_TRUE_COLOR_ALPHA, 0, 0, {px}, {px} }},")
        L.append(f"    sizeof(ce{px}_{cp:04x}), ce{px}_{cp:04x} }};")
        L.append("")
    L.append("// Emitted in codepoint order; the compiler turns the switch into a jump")
    L.append("// table or decision tree, so lookup does not walk the list.")
    L.append(f"const lv_img_dsc_t* mclite_color_emoji_{px}(uint32_t cp) {{")
    L.append("    switch (cp) {")
    for cp in cps:
        L.append(f"        case 0x{cp:04X}: return &cd{px}_{cp:04x};")
    L.append("        default: return NULL;")
    L.append("    }")
    L.append("}")
    L.append(f"#endif  // {guard}")
    return "\n".join(L) + "\n"


def verify(cps):
    """Check the committed output WITHOUT re-baking.

    Re-generating to compare meant --check imported PIL and fetched any codepoint
    missing from the cache, so it needed Pillow and a network on CI, and a Pillow
    version bump could flag a spurious mismatch. What actually drifts is the
    codepoint SET (someone edits the picker and forgets to regenerate), so verify
    that plus the structural invariants the firmware relies on.
    """
    problems = []
    for px in sorted(SIZES):
        path = os.path.join(OUT_DIR, f"emoji_color_{px}.c")
        if not os.path.exists(path):
            problems.append(f"{os.path.basename(path)} is missing"); continue
        text = io.open(path, encoding="utf-8").read()

        have = sorted(int(x, 16) for x in re.findall(r"case 0x([0-9A-Fa-f]+):", text))
        if have != list(cps):
            missing = [f"U+{c:04X}" for c in cps if c not in have]
            extra   = [f"U+{c:04X}" for c in have if c not in cps]
            problems.append(f"{os.path.basename(path)}: codepoint set differs"
                            + (f", missing {missing}" if missing else "")
                            + (f", unexpected {extra}" if extra else ""))

        # Every glyph is a px*px image of 3-byte pixels (RGB565 + alpha).
        want = px * px * 3
        for name, body in re.findall(r"static const uint8_t (ce\d+_[0-9a-f]+)\[\] = \{(.*?)\};", text, re.S):
            n = len([b for b in body.replace("\n", "").split(",") if b.strip()])
            if n != want:
                problems.append(f"{os.path.basename(path)}: {name} has {n} bytes, expected {want}")
                break
        if f"{px}, {px} }}" not in text:
            problems.append(f"{os.path.basename(path)}: header size is not {px}x{px}")
    return problems


def main():
    check = "--check" in sys.argv
    cps = codepoints_from_source()
    print(f"  {len(cps)} codepoints from ChatScreen.cpp")

    if check:
        problems = verify(cps)
        if problems:
            print("  OUT OF SYNC:")
            for p in problems:
                print("   -", p)
            return 1
        print("  colour emoji assets are in sync")
        return 0

    for px in sorted(SIZES):
        text = render(px, cps)
        path = os.path.join(OUT_DIR, f"emoji_color_{px}.c")
        io.open(path, "w", encoding="utf-8").write(text)
        print(f"  wrote {os.path.basename(path)}  ({len(text)//1024} KB source)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
