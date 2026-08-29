#include "theme.h"
#include "../config/ConfigManager.h"
#include "../util/color.h"
#include <string.h>
#include <stddef.h>

namespace mclite {
namespace theme {

// The live palette. Defaults to DARK until applyThemeFromConfig() runs at boot.
Palette ACTIVE = PALETTE_DARK;

// ─────────────────────────── Colour emoji (lv_imgfont) ───────────────────────
// Generated glyph tables — see assets/fonts/generate_color_emoji.py. Only the two
// sizes this board actually uses are compiled in (the .c files self-gate on the
// platform macro), matching FONT_BODY / FONT_HEADING below.
#ifdef PLATFORM_TWATCH
extern "C" const lv_img_dsc_t* mclite_color_emoji_16(uint32_t cp);
extern "C" const lv_img_dsc_t* mclite_color_emoji_20(uint32_t cp);
#define MONO_BODY     (&lv_font_emoji_16)
#define MONO_HEADING  (&lv_font_emoji_20)
#define COLOR_LOOKUP_BODY     mclite_color_emoji_16
#define COLOR_LOOKUP_HEADING  mclite_color_emoji_20
#else
extern "C" const lv_img_dsc_t* mclite_color_emoji_12(uint32_t cp);
extern "C" const lv_img_dsc_t* mclite_color_emoji_14(uint32_t cp);
#define MONO_BODY     (&lv_font_emoji_12)
#define MONO_HEADING  (&lv_font_emoji_14)
#define COLOR_LOOKUP_BODY     mclite_color_emoji_12
#define COLOR_LOOKUP_HEADING  mclite_color_emoji_14
#endif

static lv_font_t* s_colorBody    = nullptr;
static lv_font_t* s_colorHeading = nullptr;
// Set once initColorEmoji() has run. Without it, a genuine OOM (both
// allocations fail) meant every FONT_BODY/FONT_HEADING expansion -- i.e.
// every label on every screen build -- retried two lv_mem_allocs forever.
static bool       s_colorTried   = false;

#if LV_USE_IMGFONT
static bool colorEmojiPathCb(const lv_font_t* font, void* img_src, uint16_t,
                             uint32_t unicode, uint32_t) {
    const lv_img_dsc_t* d = (font == s_colorHeading) ? COLOR_LOOKUP_HEADING(unicode)
                                                     : COLOR_LOOKUP_BODY(unicode);
    if (!d) return false;   // not in the baked set → LVGL walks on to the mono font
    lv_memcpy(img_src, d, sizeof(lv_img_dsc_t));
    // Defensive, and currently a no-op: lv_imgfont hands every glyph back through
    // the SAME scratch buffer and lv_img_cache keys on the source pointer, so a
    // live cache would replay the first decoded emoji for all of them. This build
    // never sets LV_IMG_CACHE_DEF_SIZE, so LVGL defaults it to 0 and both the
    // cache and this function compile away (lv_img_cache.c). Keep the call: it
    // costs nothing and it is what makes enabling the cache safe later. Other
    // images (map tiles) have distinct pointers, so their caching is unaffected.
    lv_img_cache_invalidate_src(img_src);
    return true;
}
#endif

#if LV_USE_IMGFONT
// lv_imgfont_create() sets line_height to the glyph size and base_line to 0. Those
// are the metrics LVGL lays every line out with, because the label's own font wins
// over whatever the fallback chain resolves a glyph to. Left alone, a 12px imgfont
// gave the 12px text a 12px line box instead of the mono emoji font's real 17px
// one, and the bottoms of the text got clipped. Adopt the mono font's metrics so
// the text keeps its true line height.
//
// lv_draw_sw_letter puts the baseline at (line_height - base_line), so this moves
// it from 12 (12-0) to 13 (17-4) for T-Deck body text -- one pixel, onto the mono
// font's real baseline, which is where it belonged. box_h <= line_height - base_line
// in all four board/size combinations, so no glyph clips or overruns.
static void adoptMetrics(lv_font_t* f, const lv_font_t* mono) {
    if (!f) return;
    f->line_height         = mono->line_height;
    f->base_line           = mono->base_line;
    f->underline_position  = mono->underline_position;
    f->underline_thickness = mono->underline_thickness;
    f->fallback            = mono;   // unbaked glyphs fall through to mono
}
#endif

void initColorEmoji() {
#if LV_USE_IMGFONT
    // Guard on BOTH: if only one allocation succeeded, guarding on s_colorBody
    // alone let fontBody() re-enter and reassign s_colorHeading, leaking the old
    // one and leaving already-styled labels pointing at a font that no longer
    // matches s_colorHeading -- so colorEmojiPathCb would resolve them through the
    // body-size lookup and draw undersized glyphs.
    if (s_colorTried) return;   // built, or tried and failed -- never retry
    s_colorTried = true;
    s_colorBody    = lv_imgfont_create(FONT_BODY_PX, colorEmojiPathCb);
    s_colorHeading = lv_imgfont_create(FONT_HEADING_PX, colorEmojiPathCb);
    // On OOM leave the pointers null; fontBody()/fontHeading() keep returning mono.
    adoptMetrics(s_colorBody,    MONO_BODY);
    adoptMetrics(s_colorHeading, MONO_HEADING);
#endif
}

// Consult the live config rather than a boot-time snapshot, and build the imgfont
// lazily the first time it is actually wanted, so a device that never enables
// colour emoji never pays the allocation.
//
// main.cpp only calls initColorEmoji() when the setting is on, so a device with
// colour emoji off really does skip the allocation.
//
// Note the Settings toggle still reboots to apply. Screens that are rebuilt per
// show would pick the new font up immediately, but ChatScreen is created once at
// boot and its labels keep the font pointer they were handed, so a live toggle
// would leave chat looking unchanged. Rebooting keeps every screen consistent.
const lv_font_t* fontBody() {
    if (!ConfigManager::instance().config().display.colorEmoji) return MONO_BODY;
    if (!s_colorBody) initColorEmoji();
    return s_colorBody ? s_colorBody : MONO_BODY;
}
const lv_font_t* fontHeading() {
    if (!ConfigManager::instance().config().display.colorEmoji) return MONO_HEADING;
    if (!s_colorHeading) initColorEmoji();
    return s_colorHeading ? s_colorHeading : MONO_HEADING;
}

const Palette* builtinPaletteByName(const char* name) {
    if (!name) return nullptr;
    if (!strcmp(name, "dark"))          return &PALETTE_DARK;
    if (!strcmp(name, "light"))         return &PALETTE_LIGHT;
    if (!strcmp(name, "amber"))         return &PALETTE_AMBER;
    if (!strcmp(name, "high_contrast")) return &PALETTE_HIGHCON;
    return nullptr;
}

namespace {

// Canonical color-key name → Palette member offset. Keep in sync with the
// Palette struct order in theme.h. Custom themes reference these keys.
struct ColorKey { const char* key; size_t offset; };
const ColorKey COLOR_KEYS[] = {
    {"bg_primary",       offsetof(Palette, bg_primary)},
    {"bg_secondary",     offsetof(Palette, bg_secondary)},
    {"bg_status_bar",    offsetof(Palette, bg_status_bar)},
    {"bg_input",         offsetof(Palette, bg_input)},
    {"text_primary",     offsetof(Palette, text_primary)},
    {"text_secondary",   offsetof(Palette, text_secondary)},
    {"text_timestamp",   offsetof(Palette, text_timestamp)},
    {"bubble_self",      offsetof(Palette, bubble_self)},
    {"bubble_self_meta", offsetof(Palette, bubble_self_meta)},
    {"bubble_them",      offsetof(Palette, bubble_them)},
    {"bubble_self_text", offsetof(Palette, bubble_self_text)},
    {"accent",           offsetof(Palette, accent)},
    {"unread_dot",       offsetof(Palette, unread_dot)},
    {"online_dot",       offsetof(Palette, online_dot)},
    {"battery_low",      offsetof(Palette, battery_low)},
    {"battery_ok",       offsetof(Palette, battery_ok)},
    {"gps_last_known",   offsetof(Palette, gps_last_known)},
    {"offgrid_accent",   offsetof(Palette, offgrid_accent)},
    {"room_accent",      offsetof(Palette, room_accent)},
    {"scrim",            offsetof(Palette, scrim)},
    {"text_on_accent",   offsetof(Palette, text_on_accent)},
};

// Apply one "key":"#RRGGBB" override to a palette. Unknown keys / bad hex ignored.
void applyOverride(Palette& p, const String& key, const String& hex) {
    uint32_t rgb;
    if (!parseHexRGB(hex.c_str(), rgb)) return;
    lv_color_t c = lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    for (const auto& ck : COLOR_KEYS) {
        if (key == ck.key) {
            *reinterpret_cast<lv_color_t*>(reinterpret_cast<char*>(&p) + ck.offset) = c;
            return;
        }
    }
}

}  // namespace

void applyThemeFromConfig() {
    const auto& cfg = ConfigManager::instance().config();
    const String& name = cfg.display.theme;

    // Built-in?
    if (const Palette* b = builtinPaletteByName(name.c_str())) {
        ACTIVE = *b;
        return;
    }
    // Custom theme: start from its base built-in, apply overrides.
    for (const auto& ct : cfg.display.customThemes) {
        if (name == ct.name) {
            const Palette* base = builtinPaletteByName(ct.base.c_str());
            ACTIVE = base ? *base : PALETTE_DARK;
            for (const auto& c : ct.colors) applyOverride(ACTIVE, c.first, c.second);
            return;
        }
    }
    // Unknown → safe default.
    ACTIVE = PALETTE_DARK;
}

}  // namespace theme
}  // namespace mclite
