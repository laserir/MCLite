#pragma once

#include <lvgl.h>

#include "../hal/boards/board.h"

// Generated OpenMoji emoji fonts (src/ui/fonts/, board-gated). Each carries a
// Montserrat `.fallback`, so assigning one to a label renders emoji from it and
// all ASCII from Montserrat (visually identical). T-Deck links 12/14, T-Watch
// 16/20 — the declarations for the other board's sizes are unreferenced externs.
LV_FONT_DECLARE(lv_font_emoji_12)
LV_FONT_DECLARE(lv_font_emoji_14)
LV_FONT_DECLARE(lv_font_emoji_16)
LV_FONT_DECLARE(lv_font_emoji_20)

// Icon aliases — simple text characters that always render correctly
#define ICON_DM       "@"                  // DM contact
#define ICON_CHANNEL  "#"                  // Public channel
#define ICON_PRIVATE  "*"                  // Private channel
#define ICON_ROOM     "R"                  // Room server
#define ICON_LOCK     LV_SYMBOL_CLOSE      // PIN lock screen

namespace mclite {
namespace theme {

// ─── Colors: runtime-selectable palette ───────────────────────────────────
// Colors used to be compile-time constexpr; they're now members of a Palette
// chosen at boot (see theme.cpp / applyThemeFromConfig) so the UI can be themed.
// Call sites use accessor functions: theme::BG_PRIMARY(), theme::ACCENT(), …
// Spacing / fonts / sizes further below stay constexpr (not themed).
//
// Member order is the canonical order — keep it in sync with the ColorKey
// registry in theme.cpp and with every built-in palette initializer below.
struct Palette {
    lv_color_t bg_primary, bg_secondary, bg_status_bar, bg_input;
    lv_color_t text_primary, text_secondary, text_timestamp;
    lv_color_t bubble_self, bubble_self_meta, bubble_them, bubble_self_text;
    lv_color_t accent, unread_dot, online_dot, battery_low, battery_ok,
               gps_last_known, offgrid_accent, room_accent;
    lv_color_t scrim, text_on_accent;
};

// DARK — the original values; appearance unchanged from the pre-theme build.
constexpr Palette PALETTE_DARK = {
    LV_COLOR_MAKE(0x1A, 0x1A, 0x2E),  // bg_primary       deep navy
    LV_COLOR_MAKE(0x16, 0x21, 0x3E),  // bg_secondary
    LV_COLOR_MAKE(0x0F, 0x0F, 0x1A),  // bg_status_bar    darkest
    LV_COLOR_MAKE(0x22, 0x22, 0x3A),  // bg_input
    LV_COLOR_MAKE(0xE8, 0xE8, 0xF0),  // text_primary     bright white
    LV_COLOR_MAKE(0x88, 0x88, 0xAA),  // text_secondary   muted
    LV_COLOR_MAKE(0x66, 0x66, 0x88),  // text_timestamp   dim
    LV_COLOR_MAKE(0x00, 0x5A, 0xD4),  // bubble_self      blue (outgoing)
    LV_COLOR_MAKE(0x99, 0xBB, 0xEE),  // bubble_self_meta light blue
    LV_COLOR_MAKE(0x2A, 0x2A, 0x45),  // bubble_them      dark gray (incoming)
    LV_COLOR_MAKE(0xE8, 0xE8, 0xF0),  // bubble_self_text == text_primary (dark)
    LV_COLOR_MAKE(0x00, 0x7A, 0xFF),  // accent           bright blue
    LV_COLOR_MAKE(0x00, 0xCC, 0x66),  // unread_dot       green
    LV_COLOR_MAKE(0x00, 0xCC, 0x66),  // online_dot       green
    LV_COLOR_MAKE(0xFF, 0x44, 0x44),  // battery_low      red
    LV_COLOR_MAKE(0x00, 0xCC, 0x66),  // battery_ok       green
    LV_COLOR_MAKE(0xFF, 0xAA, 0x00),  // gps_last_known   amber
    LV_COLOR_MAKE(0xFF, 0x8C, 0x00),  // offgrid_accent   warm orange
    LV_COLOR_MAKE(0xA2, 0x59, 0xFF),  // room_accent      purple
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  // scrim            modal backdrop (black)
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  // text_on_accent   white
};

// LIGHT — bright surfaces, dark text; for daylight / indoor use.
constexpr Palette PALETTE_LIGHT = {
    LV_COLOR_MAKE(0xEE, 0xF0, 0xF4),  // bg_primary
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  // bg_secondary    cards
    LV_COLOR_MAKE(0xDC, 0xE0, 0xE8),  // bg_status_bar
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  // bg_input
    LV_COLOR_MAKE(0x1A, 0x1A, 0x2E),  // text_primary    dark navy
    LV_COLOR_MAKE(0x5A, 0x62, 0x75),  // text_secondary
    LV_COLOR_MAKE(0x90, 0x98, 0xA8),  // text_timestamp
    LV_COLOR_MAKE(0x0A, 0x6C, 0xF0),  // bubble_self     blue
    LV_COLOR_MAKE(0xD2, 0xE4, 0xFF),  // bubble_self_meta
    LV_COLOR_MAKE(0xE6, 0xE9, 0xF2),  // bubble_them     light gray
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  // bubble_self_text white on blue
    LV_COLOR_MAKE(0x0A, 0x6C, 0xF0),  // accent
    LV_COLOR_MAKE(0x0F, 0xA9, 0x58),  // unread_dot      green
    LV_COLOR_MAKE(0x0F, 0xA9, 0x58),  // online_dot
    LV_COLOR_MAKE(0xE0, 0x31, 0x31),  // battery_low
    LV_COLOR_MAKE(0x0F, 0xA9, 0x58),  // battery_ok
    LV_COLOR_MAKE(0xD9, 0x82, 0x00),  // gps_last_known
    LV_COLOR_MAKE(0xE0, 0x7A, 0x00),  // offgrid_accent
    LV_COLOR_MAKE(0x8A, 0x3F, 0xFE),  // room_accent
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  // scrim
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  // text_on_accent
};

// AMBER — "military" night mode: warm-black surfaces, amber text, preserves
// night vision.
constexpr Palette PALETTE_AMBER = {
    LV_COLOR_MAKE(0x0A, 0x06, 0x00),  // bg_primary
    LV_COLOR_MAKE(0x16, 0x0E, 0x00),  // bg_secondary
    LV_COLOR_MAKE(0x05, 0x03, 0x00),  // bg_status_bar
    LV_COLOR_MAKE(0x1C, 0x12, 0x00),  // bg_input
    LV_COLOR_MAKE(0xFF, 0xB0, 0x00),  // text_primary    amber
    LV_COLOR_MAKE(0xB3, 0x7A, 0x00),  // text_secondary
    LV_COLOR_MAKE(0x7A, 0x52, 0x00),  // text_timestamp
    LV_COLOR_MAKE(0x3A, 0x24, 0x00),  // bubble_self     dark amber
    LV_COLOR_MAKE(0xC8, 0x90, 0x2A),  // bubble_self_meta
    LV_COLOR_MAKE(0x1E, 0x13, 0x00),  // bubble_them
    LV_COLOR_MAKE(0xFF, 0xC0, 0x20),  // bubble_self_text bright amber
    LV_COLOR_MAKE(0xFF, 0x8C, 0x00),  // accent          bright amber
    LV_COLOR_MAKE(0xFF, 0xB0, 0x00),  // unread_dot
    LV_COLOR_MAKE(0xFF, 0xB0, 0x00),  // online_dot
    LV_COLOR_MAKE(0xFF, 0x30, 0x00),  // battery_low     red still reads as alarm
    LV_COLOR_MAKE(0xFF, 0xB0, 0x00),  // battery_ok
    LV_COLOR_MAKE(0xFF, 0xC0, 0x20),  // gps_last_known
    LV_COLOR_MAKE(0xFF, 0x6A, 0x00),  // offgrid_accent
    LV_COLOR_MAKE(0xFF, 0xA0, 0x40),  // room_accent
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  // scrim
    LV_COLOR_MAKE(0x1A, 0x0E, 0x00),  // text_on_accent  dark on bright amber
};

// HIGH-CONTRAST — pure black, bright text/accents for maximum legibility.
constexpr Palette PALETTE_HIGHCON = {
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  // bg_primary
    LV_COLOR_MAKE(0x0A, 0x0A, 0x0A),  // bg_secondary
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  // bg_status_bar
    LV_COLOR_MAKE(0x14, 0x14, 0x14),  // bg_input
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  // text_primary    white
    LV_COLOR_MAKE(0xC8, 0xC8, 0xC8),  // text_secondary
    LV_COLOR_MAKE(0xA0, 0xA0, 0xA0),  // text_timestamp
    LV_COLOR_MAKE(0x00, 0x50, 0xFF),  // bubble_self
    LV_COLOR_MAKE(0xD0, 0xE0, 0xFF),  // bubble_self_meta
    LV_COLOR_MAKE(0x20, 0x20, 0x20),  // bubble_them
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  // bubble_self_text
    LV_COLOR_MAKE(0x00, 0xA0, 0xFF),  // accent          bright blue
    LV_COLOR_MAKE(0x00, 0xFF, 0x66),  // unread_dot
    LV_COLOR_MAKE(0x00, 0xFF, 0x66),  // online_dot
    LV_COLOR_MAKE(0xFF, 0x20, 0x20),  // battery_low
    LV_COLOR_MAKE(0x00, 0xFF, 0x66),  // battery_ok
    LV_COLOR_MAKE(0xFF, 0xC0, 0x00),  // gps_last_known
    LV_COLOR_MAKE(0xFF, 0x80, 0x00),  // offgrid_accent
    LV_COLOR_MAKE(0xC0, 0x80, 0xFF),  // room_accent
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  // scrim
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  // text_on_accent  black on bright accent
};

// The active palette — defined in theme.cpp, set once at boot by
// applyThemeFromConfig() before any screen is built.
extern Palette ACTIVE;

inline lv_color_t BG_PRIMARY()       { return ACTIVE.bg_primary; }
inline lv_color_t BG_SECONDARY()     { return ACTIVE.bg_secondary; }
inline lv_color_t BG_STATUS_BAR()    { return ACTIVE.bg_status_bar; }
inline lv_color_t BG_INPUT()         { return ACTIVE.bg_input; }
inline lv_color_t TEXT_PRIMARY()     { return ACTIVE.text_primary; }
inline lv_color_t TEXT_SECONDARY()   { return ACTIVE.text_secondary; }
inline lv_color_t TEXT_TIMESTAMP()   { return ACTIVE.text_timestamp; }
inline lv_color_t BUBBLE_SELF()      { return ACTIVE.bubble_self; }
inline lv_color_t BUBBLE_SELF_META() { return ACTIVE.bubble_self_meta; }
inline lv_color_t BUBBLE_THEM()      { return ACTIVE.bubble_them; }
inline lv_color_t BUBBLE_SELF_TEXT() { return ACTIVE.bubble_self_text; }
inline lv_color_t ACCENT()           { return ACTIVE.accent; }
inline lv_color_t UNREAD_DOT()       { return ACTIVE.unread_dot; }
inline lv_color_t ONLINE_DOT()       { return ACTIVE.online_dot; }
inline lv_color_t BATTERY_LOW()      { return ACTIVE.battery_low; }
inline lv_color_t BATTERY_OK()       { return ACTIVE.battery_ok; }
inline lv_color_t GPS_LAST_KNOWN()   { return ACTIVE.gps_last_known; }
inline lv_color_t OFFGRID_ACCENT()   { return ACTIVE.offgrid_accent; }
inline lv_color_t ROOM_ACCENT()      { return ACTIVE.room_accent; }
inline lv_color_t SCRIM()            { return ACTIVE.scrim; }
inline lv_color_t TEXT_ON_ACCENT()   { return ACTIVE.text_on_accent; }

// Resolve config.display.theme (built-in or custom) into ACTIVE. Call once at
// boot, before the UI is created.
void applyThemeFromConfig();
// Look up a built-in palette by canonical name ("dark"/"light"/"amber"/
// "high_contrast"); nullptr if unknown. Custom themes are resolved separately.
const Palette* builtinPaletteByName(const char* name);

// Spacing
constexpr int PAD_SMALL   = 4;
constexpr int PAD_MEDIUM  = 8;
constexpr int PAD_LARGE   = 12;

// Safe-area insets for displays with rounded bezel corners. T-Deck's rect LCD
// uses 0; T-Watch's AMOLED clips into the panel at each rounded corner —
// vertical clipping is more aggressive than horizontal.
// T-Watch's rounded AMOLED used to push *every* content widget down by
// SAFE_AREA_TOP/BOTTOM=24 to clear the bezel curves. R8 stops doing that:
// the status bar at the top and the new footer at the bottom now ABSORB
// the vertical safe area (their inner pad_top / pad_bottom keep content
// out of the rounded zones), so widgets in between can use the full
// vertical band between the two bars without further offset.
#ifdef PLATFORM_TWATCH
constexpr int SAFE_AREA_TOP    = 0;    // was 24 — now absorbed by STATUS_BAR
constexpr int SAFE_AREA_BOTTOM = 0;    // was 24 — now absorbed by FOOTER
constexpr int SAFE_AREA_LEFT   = 20;
constexpr int SAFE_AREA_RIGHT  = 20;
#else
constexpr int SAFE_AREA_TOP    = 0;
constexpr int SAFE_AREA_BOTTOM = 0;
constexpr int SAFE_AREA_LEFT   = 0;
constexpr int SAFE_AREA_RIGHT  = 0;
#endif

// Width of the content area after horizontal safe-area insets. Equals the full
// display width on T-Deck; 32px narrower on T-Watch. Use this (centered) for
// any content-bearing widget (status bar, chat header/input bar, list rows).
// Full-screen backgrounds can stay at Display::width() — the rounded corners
// just clip the background fill.
constexpr int CONTENT_WIDTH = BOARD_DISP_W - SAFE_AREA_LEFT - SAFE_AREA_RIGHT;

// Status bar — taller on T-Watch to fit a two-row layout (centered device
// name on top, finger-sized centered icons below). R8: bar height grows
// from 72 to 96 to absorb the 24 px rounded-top safe area; inner pad_top
// keeps content out of the corner. The new FOOTER does the same on the
// bottom and hosts the clock. T-Deck stays single-row, no footer.
#ifdef PLATFORM_TWATCH
constexpr int STATUS_BAR_HEIGHT   = 96;
constexpr int FOOTER_HEIGHT       = 64;
constexpr int STATUS_BAR_PAD_V    = 20;  // inner top pad — clear rounded top
constexpr int FOOTER_PAD_V        = 20;  // inner bottom pad — clear rounded bottom
constexpr int STATUS_BAR_ICON_GAP = PAD_LARGE;
#else
constexpr int STATUS_BAR_HEIGHT = 24;
constexpr int FOOTER_HEIGHT     = 0;     // T-Deck has no footer
#endif

// Inner horizontal padding for full-width bars (status bar, chat header, chat
// input bar). On T-Deck these stay at PAD_SMALL so visuals are unchanged. On
// T-Watch the inner content needs to clear the rounded-corner safe area AND
// have generous touch breathing room — status bar gets the most, input bar
// slightly more than the header.
#ifdef PLATFORM_TWATCH
constexpr int STATUS_BAR_PAD_HOR  = SAFE_AREA_LEFT + PAD_LARGE;   // 32
constexpr int INPUT_BAR_PAD_HOR   = SAFE_AREA_LEFT + PAD_MEDIUM;  // 28
constexpr int CHAT_HEADER_PAD_HOR = SAFE_AREA_LEFT + PAD_SMALL;   // 24
#else
constexpr int STATUS_BAR_PAD_HOR  = PAD_SMALL;
constexpr int INPUT_BAR_PAD_HOR   = PAD_SMALL;
constexpr int CHAT_HEADER_PAD_HOR = PAD_SMALL;
#endif

// Chat header / input bar heights. Touch finger targets on T-Watch need to be
// larger than trackball-click targets on T-Deck.
#ifdef PLATFORM_TWATCH
constexpr int CHAT_HEADER_HEIGHT = 56;
constexpr int CHAT_INPUT_HEIGHT  = 72;
#else
constexpr int CHAT_HEADER_HEIGHT = 28;
constexpr int CHAT_INPUT_HEIGHT  = 36;
#endif

// Button + textarea sizes. T-Deck values match the prior hardcoded numbers
// so the binary stays bit-identical; T-Watch bumps everything for finger
// touch within the now-taller header/input-bar slots.
#ifdef PLATFORM_TWATCH
constexpr int BTN_ACTION_W      = 48;  // chat input bar action buttons (canned, gps)
constexpr int BTN_ACTION_H      = 48;
constexpr int BTN_SEND_W        = 60;
constexpr int BTN_SEND_H        = 48;
constexpr int BTN_HEADER_BACK_W = 48;
constexpr int BTN_HEADER_BACK_H = 40;
constexpr int BTN_HEADER_ICON_W = 48;  // header icon buttons (HeardAdverts, Admin close)
constexpr int BTN_HEADER_ICON_H = 40;
constexpr int CHAT_NAME_BTN_H   = 40;
constexpr int CHAT_TEXTAREA_H   = 48;
#else
constexpr int BTN_ACTION_W      = 28;
constexpr int BTN_ACTION_H      = 28;
constexpr int BTN_SEND_W        = 50;
constexpr int BTN_SEND_H        = 28;
constexpr int BTN_HEADER_BACK_W = 30;
constexpr int BTN_HEADER_BACK_H = 20;
constexpr int BTN_HEADER_ICON_W = 32;
constexpr int BTN_HEADER_ICON_H = 24;
constexpr int CHAT_NAME_BTN_H   = 20;
constexpr int CHAT_TEXTAREA_H   = 28;
#endif

// Standard modal/text widths — derived from CONTENT_WIDTH so they fit
// each board. T-Deck: 320 - 40 = 280 (matches prior 280 literal).
constexpr int MODAL_TEXT_WIDTH = CONTENT_WIDTH - 40;

// Chat bubbles
constexpr int BUBBLE_RADIUS     = 8;
constexpr int BUBBLE_MAX_WIDTH  = BOARD_DISP_W * 3 / 4;  // ~75% of screen width
constexpr int BUBBLE_PAD        = 6;

// Conversation list — taller rows on T-Watch for finger touch + bigger fonts.
#ifdef PLATFORM_TWATCH
constexpr int CONVO_ROW_HEIGHT  = 64;
#else
constexpr int CONVO_ROW_HEIGHT  = 48;
#endif

// Body/heading fonts are resolved at runtime so colour emoji can be swapped in.
// The colour font is an lv_imgfont whose fallback is the existing monochrome
// emoji font (which in turn falls back to Montserrat), so the chain is
//   colour subset -> mono emoji -> ASCII
// and anything outside the baked set still renders exactly as before. Returns
// the mono font directly when display.color_emoji is off or creation failed.
#ifdef PLATFORM_TWATCH
constexpr int FONT_BODY_PX = 16, FONT_HEADING_PX = 20;
#else
constexpr int FONT_BODY_PX = 12, FONT_HEADING_PX = 14;
#endif
const lv_font_t* fontBody();
const lv_font_t* fontHeading();
void initColorEmoji();     // call once after config load, before the UI is built

// Fonts — T-Watch bumps every level up by 2-4pt so labels are readable at
// arm's length. T-Deck unchanged.
#ifdef PLATFORM_TWATCH
#define FONT_SMALL          &lv_font_montserrat_14
#define FONT_NORMAL         &lv_font_montserrat_16
#define FONT_LARGE          &lv_font_montserrat_20
#define FONT_TITLE          &lv_font_montserrat_24
#define FONT_STATUSBAR_ICON &lv_font_montserrat_28  // 2x current icon size for finger taps
// Semantic fonts — one step larger than the size-named FONT_* on T-Watch so body
// text and section headings read comfortably at arm's length. Emoji fonts are
// primary here; they fall back to Montserrat for ASCII (--lv-fallback), so emoji
// in any message render inline. Used in chat bubbles, the chat header, admin rows.
#define FONT_BODY     mclite::theme::fontBody()     // colour-emoji font when enabled, else mono
#define FONT_HEADING  mclite::theme::fontHeading()
#else
#define FONT_SMALL    &lv_font_montserrat_12
#define FONT_NORMAL   &lv_font_montserrat_14
#define FONT_LARGE    &lv_font_montserrat_16
#define FONT_TITLE    &lv_font_montserrat_20
#define FONT_BODY     mclite::theme::fontBody()     // colour-emoji font when enabled, else mono
#define FONT_HEADING  mclite::theme::fontHeading()
#endif

}  // namespace theme
}  // namespace mclite
