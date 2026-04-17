#pragma once

#include <lvgl.h>

// Icon aliases — simple text characters that always render correctly
#define ICON_DM       "@"                  // DM contact
#define ICON_CHANNEL  "#"                  // Public channel
#define ICON_PRIVATE  "*"                  // Private channel
#define ICON_LOCK     LV_SYMBOL_CLOSE      // PIN lock screen

namespace mclite {
namespace theme {

// Colors — dark theme, high contrast for daylight readability
constexpr lv_color_t BG_PRIMARY      = LV_COLOR_MAKE(0x1A, 0x1A, 0x2E);  // Deep navy
constexpr lv_color_t BG_SECONDARY    = LV_COLOR_MAKE(0x16, 0x21, 0x3E);  // Slightly lighter
constexpr lv_color_t BG_STATUS_BAR   = LV_COLOR_MAKE(0x0F, 0x0F, 0x1A);  // Darkest
constexpr lv_color_t BG_INPUT        = LV_COLOR_MAKE(0x22, 0x22, 0x3A);  // Input field

constexpr lv_color_t TEXT_PRIMARY    = LV_COLOR_MAKE(0xE8, 0xE8, 0xF0);  // Bright white
constexpr lv_color_t TEXT_SECONDARY  = LV_COLOR_MAKE(0x88, 0x88, 0xAA);  // Muted
constexpr lv_color_t TEXT_TIMESTAMP  = LV_COLOR_MAKE(0x66, 0x66, 0x88);  // Dim

constexpr lv_color_t BUBBLE_SELF      = LV_COLOR_MAKE(0x00, 0x5A, 0xD4);  // Blue (outgoing)
constexpr lv_color_t BUBBLE_SELF_META = LV_COLOR_MAKE(0x99, 0xBB, 0xEE);  // Light blue (timestamp on blue)
constexpr lv_color_t BUBBLE_THEM      = LV_COLOR_MAKE(0x2A, 0x2A, 0x45);  // Dark gray (incoming)

constexpr lv_color_t ACCENT          = LV_COLOR_MAKE(0x00, 0x7A, 0xFF);  // Bright blue
constexpr lv_color_t UNREAD_DOT      = LV_COLOR_MAKE(0x00, 0xCC, 0x66);  // Green
constexpr lv_color_t ONLINE_DOT      = LV_COLOR_MAKE(0x00, 0xCC, 0x66);  // Green
constexpr lv_color_t BATTERY_LOW     = LV_COLOR_MAKE(0xFF, 0x44, 0x44);  // Red
constexpr lv_color_t BATTERY_OK      = LV_COLOR_MAKE(0x00, 0xCC, 0x66);  // Green
constexpr lv_color_t GPS_LAST_KNOWN  = LV_COLOR_MAKE(0xFF, 0xAA, 0x00);  // Amber/yellow

// Spacing
constexpr int PAD_SMALL   = 4;
constexpr int PAD_MEDIUM  = 8;
constexpr int PAD_LARGE   = 12;

// Status bar
constexpr int STATUS_BAR_HEIGHT = 24;

// Chat bubbles
constexpr int BUBBLE_RADIUS     = 8;
constexpr int BUBBLE_MAX_WIDTH  = 240;  // ~75% of screen width
constexpr int BUBBLE_PAD        = 6;

// Conversation list
constexpr int CONVO_ROW_HEIGHT  = 48;

// Fonts
#define FONT_SMALL    &lv_font_montserrat_12
#define FONT_NORMAL   &lv_font_montserrat_14
#define FONT_LARGE    &lv_font_montserrat_16
#define FONT_TITLE    &lv_font_montserrat_20

}  // namespace theme
}  // namespace mclite
