#include "DeviceSettingsScreen.h"
#include <Arduino.h>
#include <vector>
#include "UIManager.h"
#include "theme.h"
#include "../config/ConfigManager.h"
#include "../hal/Display.h"
#include "../hal/IInput.h"
#include "../hal/Speaker.h"
#include "../i18n/I18n.h"
#include "../config/defaults.h"

namespace mclite {

namespace {
// Friendly name for a theme: translated label for built-ins, raw name for custom.
String themeDisplayName(const String& name) {
    if (name == "dark")          return t("theme_dark");
    if (name == "light")         return t("theme_light");
    if (name == "amber")         return t("theme_amber");
    if (name == "high_contrast") return t("theme_high_contrast");
    return name;  // custom theme — show its own name
}
}  // namespace

void DeviceSettingsScreen::create(lv_obj_t* parent) {
    _screen = lv_win_create(parent, theme::CHAT_HEADER_HEIGHT);
    lv_obj_set_size(_screen, Display::width(),
                    Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT);
    lv_obj_align(_screen, LV_ALIGN_BOTTOM_MID, 0, -theme::FOOTER_HEIGHT);
    lv_obj_set_style_bg_color(_screen, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_set_style_radius(_screen, 0, 0);
    lv_obj_set_style_pad_all(_screen, 0, 0);
    lv_obj_set_style_pad_row(_screen, theme::PAD_SMALL, 0);

#ifdef PLATFORM_TWATCH
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(_screen, [](lv_event_t* e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_RIGHT) UIManager::instance().goHome();
    }, LV_EVENT_GESTURE, nullptr);
#endif

    // Style the header
    lv_obj_t* header = lv_win_get_header(_screen);
    lv_obj_set_style_bg_color(header, theme::BG_STATUS_BAR(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_hor(header, theme::CHAT_HEADER_PAD_HOR, 0);

    // Back button
    _backBtn = lv_win_add_btn(_screen, LV_SYMBOL_LEFT, theme::BTN_HEADER_BACK_W);
    lv_obj_set_style_bg_opa(_backBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(_backBtn, 0, 0);
    lv_obj_set_style_border_width(_backBtn, 0, 0);
    lv_obj_add_event_cb(_backBtn, backBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* backLbl = lv_obj_get_child(_backBtn, 0);
    lv_obj_set_style_text_font(backLbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(backLbl, theme::ACCENT(), 0);

    // Title
    lv_obj_t* title = lv_win_add_title(_screen, t("device_settings_title"));
    lv_obj_set_style_text_font(title, FONT_HEADING, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY(), 0);

    // Content area
    _content = lv_win_get_content(_screen);
    lv_obj_set_style_bg_opa(_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_content, 0, 0);
    lv_obj_set_style_pad_all(_content, theme::PAD_MEDIUM, 0);
    lv_obj_set_style_pad_row(_content, theme::PAD_SMALL, 0);
    lv_obj_set_flex_flow(_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(_content, LV_OBJ_FLAG_SCROLLABLE);

#ifdef PLATFORM_TWATCH
    lv_obj_set_style_pad_hor(_content, theme::SAFE_AREA_LEFT, 0);
    lv_obj_set_style_pad_ver(_content, theme::PAD_MEDIUM, 0);
    lv_obj_set_scroll_dir(_content, LV_DIR_VER);
#endif

    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* DeviceSettingsScreen::createRowContainer(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    return row;
}

void DeviceSettingsScreen::show() {
    if (!_screen) return;

    // Clear old content
    lv_obj_clean(_content);

    const auto& cfg = ConfigManager::instance().config();

    // Helper for section headers
    auto addSection = [this](const char* title) {
        lv_obj_t* lbl = lv_label_create(_content);
        lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
        lv_obj_set_style_text_color(lbl, theme::ACCENT(), 0);
        lv_obj_set_style_pad_top(lbl, theme::PAD_MEDIUM, 0);
        lv_label_set_text(lbl, title);
    };

    // --- Device ---
    addSection(t("sec_device"));

    // Device Name — clickable row; opens inline editor
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_device_name"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        lv_label_set_text(val, (cfg.deviceName + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, nameRowCb, LV_EVENT_CLICKED, this);
    }

    // Language — clickable row; opens a chooser
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_language"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        String langDisp = cfg.language.isEmpty() ? String("English") : cfg.language;
        lv_label_set_text(val, (langDisp + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, languageRowCb, LV_EVENT_CLICKED, this);
    }

    // Boot Text — clickable row; opens inline editor
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_boot_text"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        String btText = cfg.display.bootText.length() > 0 ? cfg.display.bootText : String(t("off"));
        lv_label_set_text(val, (btText + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, bootTextRowCb, LV_EVENT_CLICKED, this);
    }

    // --- Display ---
    addSection(t("sec_display"));

    // Theme picker — clickable row; opens a chooser and reboots to apply.
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_theme"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        lv_label_set_text(val, (themeDisplayName(cfg.display.theme) + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, themeRowCb, LV_EVENT_CLICKED, this);
    }

    // Brightness — inline slider
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SMALL, 0);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_width(lbl, LV_PCT(50));
        lv_label_set_text(lbl, t("lbl_brightness"));

        _brightnessSlider = lv_slider_create(row);
        lv_obj_set_width(_brightnessSlider, LV_PCT(40));
        lv_slider_set_range(_brightnessSlider, 10, 255);
        lv_slider_set_value(_brightnessSlider, cfg.display.brightness, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_brightnessSlider, theme::ACCENT(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_brightnessSlider, theme::ACCENT(), LV_PART_KNOB);
        lv_obj_add_event_cb(_brightnessSlider, inlineSliderChangedCb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(_brightnessSlider, inlineSliderReleasedCb, LV_EVENT_RELEASED, this);

        _brightnessValLbl = lv_label_create(row);
        lv_obj_set_style_text_font(_brightnessValLbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(_brightnessValLbl, theme::TEXT_PRIMARY(), 0);
        lv_obj_set_width(_brightnessValLbl, LV_PCT(10));
        lv_label_set_text(_brightnessValLbl, String(cfg.display.brightness).c_str());
    }

    // Auto-dim seconds — inline slider
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SMALL, 0);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_width(lbl, LV_PCT(50));
        lv_label_set_text(lbl, t("lbl_auto_dim"));

        _autoDimSlider = lv_slider_create(row);
        lv_obj_set_width(_autoDimSlider, LV_PCT(40));
        lv_slider_set_range(_autoDimSlider, 0, 3600);
        lv_slider_set_value(_autoDimSlider, cfg.display.autoDimSeconds, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_autoDimSlider, theme::ACCENT(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_autoDimSlider, theme::ACCENT(), LV_PART_KNOB);
        lv_obj_add_event_cb(_autoDimSlider, inlineSliderChangedCb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(_autoDimSlider, inlineSliderReleasedCb, LV_EVENT_RELEASED, this);

        _autoDimValLbl = lv_label_create(row);
        lv_obj_set_style_text_font(_autoDimValLbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(_autoDimValLbl, theme::TEXT_PRIMARY(), 0);
        lv_obj_set_width(_autoDimValLbl, LV_PCT(10));
        String adTxt = cfg.display.autoDimSeconds > 0 ? (String(cfg.display.autoDimSeconds) + "s") : String(t("off"));
        lv_label_set_text(_autoDimValLbl, adTxt.c_str());
    }

    // Dim brightness — inline slider
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SMALL, 0);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_width(lbl, LV_PCT(50));
        lv_label_set_text(lbl, t("lbl_dim_brightness"));

        _dimBrightnessSlider = lv_slider_create(row);
        lv_obj_set_width(_dimBrightnessSlider, LV_PCT(40));
        lv_slider_set_range(_dimBrightnessSlider, 0, 255);
        lv_slider_set_value(_dimBrightnessSlider, cfg.display.dimBrightness, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_dimBrightnessSlider, theme::ACCENT(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_dimBrightnessSlider, theme::ACCENT(), LV_PART_KNOB);
        lv_obj_add_event_cb(_dimBrightnessSlider, inlineSliderChangedCb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(_dimBrightnessSlider, inlineSliderReleasedCb, LV_EVENT_RELEASED, this);

        _dimBrightnessValLbl = lv_label_create(row);
        lv_obj_set_style_text_font(_dimBrightnessValLbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(_dimBrightnessValLbl, theme::TEXT_PRIMARY(), 0);
        lv_obj_set_width(_dimBrightnessValLbl, LV_PCT(10));
        String dbTxt = cfg.display.dimBrightness > 0 ? String(cfg.display.dimBrightness) : String(t("off"));
        lv_label_set_text(_dimBrightnessValLbl, dbTxt.c_str());
    }

    // Keyboard brightness — inline slider
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SMALL, 0);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_width(lbl, LV_PCT(50));
        lv_label_set_text(lbl, t("lbl_kbd_backlight"));

        _kbdBrightnessSlider = lv_slider_create(row);
        lv_obj_set_width(_kbdBrightnessSlider, LV_PCT(40));
        lv_slider_set_range(_kbdBrightnessSlider, 1, 255);
        lv_slider_set_value(_kbdBrightnessSlider, cfg.display.kbdBrightness, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_kbdBrightnessSlider, theme::ACCENT(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_kbdBrightnessSlider, theme::ACCENT(), LV_PART_KNOB);
        lv_obj_add_event_cb(_kbdBrightnessSlider, inlineSliderChangedCb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(_kbdBrightnessSlider, inlineSliderReleasedCb, LV_EVENT_RELEASED, this);

        _kbdBrightnessValLbl = lv_label_create(row);
        lv_obj_set_style_text_font(_kbdBrightnessValLbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(_kbdBrightnessValLbl, theme::TEXT_PRIMARY(), 0);
        lv_obj_set_width(_kbdBrightnessValLbl, LV_PCT(10));
        lv_label_set_text(_kbdBrightnessValLbl, String(cfg.display.kbdBrightness).c_str());
    }

    // Emoji picker toggle — lv_switch
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_emoji"));

        lv_obj_t* sw = lv_switch_create(row);
        lv_obj_set_style_bg_color(sw, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (cfg.display.emoji) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, emojiToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // --- Sound ---
    addSection(t("sec_sound"));

    // Sound enabled — lv_switch
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_sound"));

        lv_obj_t* sw = lv_switch_create(row);
        lv_obj_set_style_bg_color(sw, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (cfg.soundEnabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, soundToggleCb, LV_EVENT_VALUE_CHANGED, this);
    }

    // SOS Keyword — clickable row; opens inline editor
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_sos_keyword"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        lv_label_set_text(val, (cfg.sosKeyword + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, sosKeywordRowCb, LV_EVENT_CLICKED, this);
    }

    // SOS Repeat — inline slider
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SMALL, 0);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_width(lbl, LV_PCT(50));
        lv_label_set_text(lbl, t("lbl_sos_repeat"));

        _sosRepeatSlider = lv_slider_create(row);
        lv_obj_set_width(_sosRepeatSlider, LV_PCT(40));
        lv_slider_set_range(_sosRepeatSlider, 1, 10);
        lv_slider_set_value(_sosRepeatSlider, cfg.sosRepeat, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_sosRepeatSlider, theme::ACCENT(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_sosRepeatSlider, theme::ACCENT(), LV_PART_KNOB);
        lv_obj_add_event_cb(_sosRepeatSlider, inlineSliderChangedCb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(_sosRepeatSlider, inlineSliderReleasedCb, LV_EVENT_RELEASED, this);

        _sosRepeatValLbl = lv_label_create(row);
        lv_obj_set_style_text_font(_sosRepeatValLbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(_sosRepeatValLbl, theme::TEXT_PRIMARY(), 0);
        lv_obj_set_width(_sosRepeatValLbl, LV_PCT(10));
        lv_label_set_text(_sosRepeatValLbl, String(cfg.sosRepeat).c_str());
        if (!cfg.soundEnabled) {
            lv_obj_add_state(_sosRepeatSlider, LV_STATE_DISABLED);
            lv_obj_add_state(_sosRepeatValLbl, LV_STATE_DISABLED);
        }
    }

    // Low Alert Enabled — lv_switch
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_low_alert"));

        lv_obj_t* sw = lv_switch_create(row);
        lv_obj_set_style_bg_color(sw, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (cfg.battery.lowAlertEnabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, lowAlertToggleCb, LV_EVENT_VALUE_CHANGED, this);
    }

    // Low Alert Threshold — inline slider
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SMALL, 0);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_width(lbl, LV_PCT(50));
        lv_label_set_text(lbl, t("lbl_low_alert_threshold"));

        _lowAlertSlider = lv_slider_create(row);
        lv_obj_set_width(_lowAlertSlider, LV_PCT(40));
        lv_slider_set_range(_lowAlertSlider, 5, 50);
        lv_slider_set_value(_lowAlertSlider, cfg.battery.lowAlertThreshold, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_lowAlertSlider, theme::ACCENT(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_lowAlertSlider, theme::ACCENT(), LV_PART_KNOB);
        lv_obj_add_event_cb(_lowAlertSlider, inlineSliderChangedCb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(_lowAlertSlider, inlineSliderReleasedCb, LV_EVENT_RELEASED, this);

        _lowAlertValLbl = lv_label_create(row);
        lv_obj_set_style_text_font(_lowAlertValLbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(_lowAlertValLbl, theme::TEXT_PRIMARY(), 0);
        lv_obj_set_width(_lowAlertValLbl, LV_PCT(10));
        lv_label_set_text(_lowAlertValLbl, (String(cfg.battery.lowAlertThreshold) + "%").c_str());
        // Disable threshold slider if low alerts are disabled
        if (!cfg.battery.lowAlertEnabled) {
            lv_obj_add_state(_lowAlertSlider, LV_STATE_DISABLED);
            lv_obj_add_state(_lowAlertValLbl, LV_STATE_DISABLED);
        }
    }

    // --- Security ---
    addSection(t("sec_security"));

    // Lock mode chooser — clickable row
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_lock_mode"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        String lockModeValue = t("off");
        if (cfg.security.lockMode == "key") lockModeValue = t("lock_key");
        else if (cfg.security.lockMode == "pin") lockModeValue = t("lock_pin");
        lv_label_set_text(val, (lockModeValue + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, lockModeRowCb, LV_EVENT_CLICKED, this);
    }

    // PIN code editor — clickable row
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_pin_code"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        String pinText = cfg.security.pinCode.length() > 0 ? String("••••") : String(t("off"));
        lv_label_set_text(val, (pinText + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, pinRowCb, LV_EVENT_CLICKED, this);
    }

    // Auto-lock chooser — clickable row
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_auto_lock"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        String autoLockValue = t("off");
        if (cfg.security.autoLock == "key") autoLockValue = t("lock_key");
        else if (cfg.security.autoLock == "pin") autoLockValue = t("lock_pin");
        lv_label_set_text(val, (autoLockValue + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, autoLockRowCb, LV_EVENT_CLICKED, this);
    }

    // --- Debug ---
    addSection(t("sec_debug"));

    // Screenshots toggle — lv_switch
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_screenshots"));

        lv_obj_t* sw = lv_switch_create(row);
        lv_obj_set_style_bg_color(sw, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (cfg.debug.screenshots) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, screenshotsToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Add content to input group so trackball can scroll
    lv_group_t* grp = lv_group_get_default();
    if (grp) {
        lv_group_remove_obj(_backBtn);
        lv_group_remove_obj(_content);
        lv_group_add_obj(grp, _backBtn);
        lv_group_add_obj(grp, _content);
        lv_group_focus_obj(_content);
        lv_group_set_editing(grp, true);
    }

    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void DeviceSettingsScreen::hide() {
    if (_screen) {
        if (_themeBtnm) hideThemePicker();
        if (_nameTextarea) hideNameEditor();
        if (_langBtnm) hideLanguagePicker();
        if (_lockModeBtnm) hideLockModePicker();
        if (_autoLockBtnm) hideAutoLockPicker();
        if (_pinTextarea) hidePinEditor();
        if (_sosKeywordTextarea) hideSosKeywordEditor();
        if (_bootTextTextarea) hideBootTextEditor();
        lv_group_t* grp = lv_group_get_default();
        if (grp) {
            lv_group_set_editing(grp, false);
            lv_group_remove_obj(_backBtn);
            lv_group_remove_obj(_content);
        }
        lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    }
}

void DeviceSettingsScreen::tick() {
    // No live-refresh needed for device settings
}

void DeviceSettingsScreen::backBtnCb(lv_event_t* e) {
    UIManager::instance().showScreen(Screen::ADMIN);
}

// Theme chooser state. File-scope so the static event callbacks can reach it:
namespace {
std::vector<String>      g_themeNames;
std::vector<String>      g_themeLabels;
std::vector<const char*> g_themeMap;

std::vector<String>      g_lockModeNames;
std::vector<String>      g_lockModeLabels;
std::vector<const char*> g_lockModeMap;

std::vector<String>      g_autoLockNames;
std::vector<String>      g_autoLockLabels;
std::vector<const char*> g_autoLockMap;
}  // namespace

void DeviceSettingsScreen::lockModeRowCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_lockModeBtnm) return;

    g_lockModeNames.clear(); g_lockModeLabels.clear(); g_lockModeMap.clear();
    g_lockModeNames.push_back("none");
    g_lockModeNames.push_back("key");
    g_lockModeNames.push_back("pin");
    g_lockModeLabels.push_back(t("off"));
    g_lockModeLabels.push_back(t("lock_key"));
    g_lockModeLabels.push_back(t("lock_pin"));
    g_lockModeLabels.push_back(t("btn_cancel"));

    for (size_t i = 0; i < g_lockModeLabels.size(); i++) {
        if (i > 0) g_lockModeMap.push_back("\n");
        g_lockModeMap.push_back(g_lockModeLabels[i].c_str());
    }
    g_lockModeMap.push_back("");

    self->_lockModeBtnm = lv_btnmatrix_create(lv_layer_top());
    lv_btnmatrix_set_map(self->_lockModeBtnm, g_lockModeMap.data());
#ifdef PLATFORM_TWATCH
    lv_coord_t rowH = 64;
#else
    lv_coord_t rowH = 26;
#endif
    lv_coord_t pickerH = (int)g_lockModeLabels.size() * rowH + 8;
    lv_coord_t maxH = Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT - 16;
    if (pickerH > maxH) pickerH = maxH;
    lv_obj_set_size(self->_lockModeBtnm, theme::MODAL_TEXT_WIDTH, pickerH);
    lv_obj_align(self->_lockModeBtnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(self->_lockModeBtnm, FONT_HEADING, 0);
    lv_obj_set_style_bg_color(self->_lockModeBtnm, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(self->_lockModeBtnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(self->_lockModeBtnm, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(self->_lockModeBtnm, 1, 0);
    lv_obj_set_style_radius(self->_lockModeBtnm, 8, 0);
    lv_obj_set_style_bg_color(self->_lockModeBtnm, theme::BG_INPUT(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(self->_lockModeBtnm, theme::TEXT_PRIMARY(), LV_PART_ITEMS);
    lv_obj_set_style_radius(self->_lockModeBtnm, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(self->_lockModeBtnm, theme::ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(self->_lockModeBtnm, theme::TEXT_ON_ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(self->_lockModeBtnm, lockModeChosenCb, LV_EVENT_VALUE_CHANGED, self);
    UIManager::instance().switchToModalGroup(self->_lockModeBtnm);
}

void DeviceSettingsScreen::autoLockRowCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_autoLockBtnm) return;

    g_autoLockNames.clear(); g_autoLockLabels.clear(); g_autoLockMap.clear();
    g_autoLockNames.push_back("none");
    g_autoLockNames.push_back("key");
    g_autoLockNames.push_back("pin");
    g_autoLockLabels.push_back(t("off"));
    g_autoLockLabels.push_back(t("lock_key"));
    g_autoLockLabels.push_back(t("lock_pin"));
    g_autoLockLabels.push_back(t("btn_cancel"));

    for (size_t i = 0; i < g_autoLockLabels.size(); i++) {
        if (i > 0) g_autoLockMap.push_back("\n");
        g_autoLockMap.push_back(g_autoLockLabels[i].c_str());
    }
    g_autoLockMap.push_back("");

    self->_autoLockBtnm = lv_btnmatrix_create(lv_layer_top());
    lv_btnmatrix_set_map(self->_autoLockBtnm, g_autoLockMap.data());
#ifdef PLATFORM_TWATCH
    lv_coord_t rowH = 64;
#else
    lv_coord_t rowH = 26;
#endif
    lv_coord_t pickerH = (int)g_autoLockLabels.size() * rowH + 8;
    lv_coord_t maxH = Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT - 16;
    if (pickerH > maxH) pickerH = maxH;
    lv_obj_set_size(self->_autoLockBtnm, theme::MODAL_TEXT_WIDTH, pickerH);
    lv_obj_align(self->_autoLockBtnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(self->_autoLockBtnm, FONT_HEADING, 0);
    lv_obj_set_style_bg_color(self->_autoLockBtnm, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(self->_autoLockBtnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(self->_autoLockBtnm, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(self->_autoLockBtnm, 1, 0);
    lv_obj_set_style_radius(self->_autoLockBtnm, 8, 0);
    lv_obj_set_style_bg_color(self->_autoLockBtnm, theme::BG_INPUT(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(self->_autoLockBtnm, theme::TEXT_PRIMARY(), LV_PART_ITEMS);
    lv_obj_set_style_radius(self->_autoLockBtnm, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(self->_autoLockBtnm, theme::ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(self->_autoLockBtnm, theme::TEXT_ON_ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(self->_autoLockBtnm, autoLockChosenCb, LV_EVENT_VALUE_CHANGED, self);
    UIManager::instance().switchToModalGroup(self->_autoLockBtnm);
}

void DeviceSettingsScreen::pinRowCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_pinTextarea) return;
    const auto& cfg = ConfigManager::instance().config();

    self->_pinOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_pinOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_pinOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_pinOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(self->_pinOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(self->_pinOverlay, 0, 0);
    lv_obj_clear_flag(self->_pinOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(self->_pinOverlay);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(lbl, t("lbl_pin_code"));
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    self->_pinTextarea = lv_textarea_create(self->_pinOverlay);
    lv_textarea_set_one_line(self->_pinTextarea, true);
    lv_textarea_set_max_length(self->_pinTextarea, 8);
    lv_textarea_set_placeholder_text(self->_pinTextarea, t("lbl_pin_code"));
    lv_textarea_set_text(self->_pinTextarea, cfg.security.pinCode.c_str());
    lv_obj_set_width(self->_pinTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(self->_pinTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(self->_pinTextarea, theme::ACCENT(), LV_STATE_FOCUSED);

    lv_obj_t* btnRow = lv_obj_create(self->_pinOverlay);
    lv_obj_set_size(btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, theme::PAD_MEDIUM, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, pinReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<DeviceSettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hidePinEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    lv_group_add_obj(g, self->_pinTextarea);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(self->_pinTextarea);
    // Mask PIN input and treat overlay as a modal group
    lv_textarea_set_password_mode(self->_pinTextarea, true);
    UIManager::instance().switchToModalGroup(self->_pinOverlay);
    IInput::instance().attachToGroup(g);
    lv_obj_add_event_cb(self->_pinTextarea, pinReadyCb, LV_EVENT_READY, self);

#ifdef PLATFORM_TWATCH
    self->_pinKbd = lv_keyboard_create(self->_pinOverlay);
    lv_keyboard_set_textarea(self->_pinKbd, self->_pinTextarea);
    lv_keyboard_set_popovers(self->_pinKbd, true);
    lv_btnmatrix_set_btn_ctrl_all(self->_pinKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(self->_pinKbd, pinReadyCb, LV_EVENT_READY, self);
    lv_obj_add_event_cb(self->_pinKbd, [](lv_event_t* ev) {
        auto* self = static_cast<DeviceSettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_pinKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hidePinEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

void DeviceSettingsScreen::pinReadyCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_pinTextarea) return;
    const char* text = lv_textarea_get_text(self->_pinTextarea);
    String newPin = text ? String(text) : String("");
    // Trim whitespace
    const char* s = newPin.c_str();
    int len = strlen(s);
    int l = 0, r = len - 1;
    while (l <= r && isspace((unsigned char)s[l])) ++l;
    while (r >= l && isspace((unsigned char)s[r])) --r;
    if (l > 0 || r < len - 1) {
        newPin = newPin.substring(l, r + 1);
    }
    if (newPin.length() > 8) newPin = newPin.substring(0, 8);

    auto& mgr = ConfigManager::instance();
    // Two-step confirmation: if no pending PIN, store and ask for confirmation
    if (self->_pendingPin.length() == 0) {
        self->_pendingPin = newPin;
        lv_textarea_set_text(self->_pinTextarea, "");
        lv_textarea_set_placeholder_text(self->_pinTextarea, t("lbl_pin_code"));
        return;
    }
    // Compare with pending entry
    if (self->_pendingPin == newPin) {
        if (mgr.config().security.pinCode != newPin) {
            mgr.config().security.pinCode = newPin;
            mgr.save();
        }
        self->_pendingPin = "";
        lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hidePinEditor(); }, self);
        return;
    }
    // Mismatch: clear pending and reset field
    self->_pendingPin = "";
    lv_textarea_set_text(self->_pinTextarea, "");
    lv_textarea_set_placeholder_text(self->_pinTextarea, t("lbl_pin_code"));
}

void DeviceSettingsScreen::lockModeChosenCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_lockModeBtnm) return;
    uint16_t idx = lv_btnmatrix_get_selected_btn(self->_lockModeBtnm);
    if (idx == LV_BTNMATRIX_BTN_NONE) return;
    if (idx < g_lockModeNames.size()) {
        auto& mgr = ConfigManager::instance();
        String newMode = g_lockModeNames[idx];
        if (mgr.config().security.lockMode != newMode) {
            mgr.config().security.lockMode = newMode;
            mgr.save();
        }
    }
    lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideLockModePicker(); }, self);
}

void DeviceSettingsScreen::autoLockChosenCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_autoLockBtnm) return;
    uint16_t idx = lv_btnmatrix_get_selected_btn(self->_autoLockBtnm);
    if (idx == LV_BTNMATRIX_BTN_NONE) return;
    if (idx < g_autoLockNames.size()) {
        auto& mgr = ConfigManager::instance();
        String newMode = g_autoLockNames[idx];
        if (mgr.config().security.autoLock != newMode) {
            mgr.config().security.autoLock = newMode;
            mgr.save();
        }
    }
    lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideAutoLockPicker(); }, self);
}

void DeviceSettingsScreen::hideLockModePicker() {
    if (!_lockModeBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    lv_obj_del_async(_lockModeBtnm);
    _lockModeBtnm = nullptr;
    if (_screen) show();
}

void DeviceSettingsScreen::hideAutoLockPicker() {
    if (!_autoLockBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    lv_obj_del_async(_autoLockBtnm);
    _autoLockBtnm = nullptr;
    if (_screen) show();
}

void DeviceSettingsScreen::hidePinEditor() {
    if (!_pinTextarea) return;
    UIManager::instance().restoreFromModalGroup();
#ifdef PLATFORM_TWATCH
    _pinKbd = nullptr;
#endif
    _pinTextarea = nullptr;
    lv_obj_del_async(_pinOverlay);
    _pinOverlay = nullptr;
    if (_screen) show();
}

void DeviceSettingsScreen::soundToggleCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    bool newVal = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.config().soundEnabled = newVal;
    mgr.save();
    Speaker::instance().setSoundEnabled(newVal);
    if (self) {
        if (self->_sosRepeatSlider) {
            if (newVal) lv_obj_clear_state(self->_sosRepeatSlider, LV_STATE_DISABLED);
            else lv_obj_add_state(self->_sosRepeatSlider, LV_STATE_DISABLED);
        }
        if (self->_sosRepeatValLbl) {
            if (newVal) lv_obj_clear_state(self->_sosRepeatValLbl, LV_STATE_DISABLED);
            else lv_obj_add_state(self->_sosRepeatValLbl, LV_STATE_DISABLED);
        }
    }
}

void DeviceSettingsScreen::lowAlertToggleCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    bool newVal = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.config().battery.lowAlertEnabled = newVal;
    mgr.save();
    if (self) {
        if (self->_lowAlertSlider) {
            if (newVal) lv_obj_clear_state(self->_lowAlertSlider, LV_STATE_DISABLED);
            else lv_obj_add_state(self->_lowAlertSlider, LV_STATE_DISABLED);
        }
        if (self->_lowAlertValLbl) {
            if (newVal) lv_obj_clear_state(self->_lowAlertValLbl, LV_STATE_DISABLED);
            else lv_obj_add_state(self->_lowAlertValLbl, LV_STATE_DISABLED);
        }
    }
}

void DeviceSettingsScreen::sosKeywordReadyCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_sosKeywordTextarea) return;
    const char* text = lv_textarea_get_text(self->_sosKeywordTextarea);
    String newKeyword = text ? String(text) : String("");
    const char* s = newKeyword.c_str();
    int len = strlen(s);
    int l = 0, r = len - 1;
    while (l <= r && isspace((unsigned char)s[l])) ++l;
    while (r >= l && isspace((unsigned char)s[r])) --r;
    if (l > 0 || r < len - 1) {
        newKeyword = newKeyword.substring(l, r + 1);
    }
    if (newKeyword.length() > 16) newKeyword = newKeyword.substring(0, 16);
    auto& mgr = ConfigManager::instance();
    if (newKeyword.length() > 0 && mgr.config().sosKeyword != newKeyword) {
        mgr.config().sosKeyword = newKeyword;
        mgr.save();
    }
    lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideSosKeywordEditor(); }, self);
}

void DeviceSettingsScreen::sosKeywordRowCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_sosKeywordTextarea) return;
    const auto& cfg = ConfigManager::instance().config();

    self->_sosKeywordOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_sosKeywordOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_sosKeywordOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_sosKeywordOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(self->_sosKeywordOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(self->_sosKeywordOverlay, 0, 0);
    lv_obj_clear_flag(self->_sosKeywordOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(self->_sosKeywordOverlay);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(lbl, t("lbl_sos_keyword"));
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    self->_sosKeywordTextarea = lv_textarea_create(self->_sosKeywordOverlay);
    lv_textarea_set_one_line(self->_sosKeywordTextarea, true);
    lv_textarea_set_max_length(self->_sosKeywordTextarea, 16);
    lv_textarea_set_placeholder_text(self->_sosKeywordTextarea, t("lbl_sos_keyword"));
    lv_textarea_set_text(self->_sosKeywordTextarea, cfg.sosKeyword.c_str());
    lv_obj_set_width(self->_sosKeywordTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(self->_sosKeywordTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(self->_sosKeywordTextarea, theme::ACCENT(), LV_STATE_FOCUSED);

    lv_obj_t* btnRow = lv_obj_create(self->_sosKeywordOverlay);
    lv_obj_set_size(btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, theme::PAD_MEDIUM, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, sosKeywordReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<DeviceSettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideSosKeywordEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    lv_group_add_obj(g, self->_sosKeywordTextarea);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(self->_sosKeywordTextarea);
    UIManager::instance().switchToModalGroup(self->_sosKeywordOverlay);
    IInput::instance().attachToGroup(g);
    lv_obj_add_event_cb(self->_sosKeywordTextarea, sosKeywordReadyCb, LV_EVENT_READY, self);

#ifdef PLATFORM_TWATCH
    self->_sosKeywordKbd = lv_keyboard_create(self->_sosKeywordOverlay);
    lv_keyboard_set_textarea(self->_sosKeywordKbd, self->_sosKeywordTextarea);
    lv_keyboard_set_popovers(self->_sosKeywordKbd, true);
    lv_btnmatrix_set_btn_ctrl_all(self->_sosKeywordKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(self->_sosKeywordKbd, sosKeywordReadyCb, LV_EVENT_READY, self);
    lv_obj_add_event_cb(self->_sosKeywordKbd, [](lv_event_t* ev) {
        auto* self = static_cast<DeviceSettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_sosKeywordKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideSosKeywordEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

void DeviceSettingsScreen::hideSosKeywordEditor() {
    if (!_sosKeywordTextarea) return;
    UIManager::instance().restoreFromModalGroup();
#ifdef PLATFORM_TWATCH
    _sosKeywordKbd = nullptr;
#endif
    _sosKeywordTextarea = nullptr;
    lv_obj_del_async(_sosKeywordOverlay);
    _sosKeywordOverlay = nullptr;
    if (_screen) show();
}

void DeviceSettingsScreen::themeRowCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_themeBtnm) return;
    const auto& cfg = ConfigManager::instance().config();

    g_themeNames.clear(); g_themeLabels.clear(); g_themeMap.clear();
    static const char* const BUILTIN[] = {"dark", "light", "amber", "high_contrast"};
    for (const char* b : BUILTIN) g_themeNames.push_back(b);
    for (const auto& ct : cfg.display.customThemes) g_themeNames.push_back(ct.name);

    for (const auto& n : g_themeNames) g_themeLabels.push_back(themeDisplayName(n));
    g_themeLabels.push_back(t("btn_cancel"));

    for (size_t i = 0; i < g_themeLabels.size(); i++) {
        if (i > 0) g_themeMap.push_back("\n");
        g_themeMap.push_back(g_themeLabels[i].c_str());
    }
    g_themeMap.push_back("");
    int count = (int)g_themeLabels.size();

    self->_themeOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_themeOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_themeOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_themeOverlay, theme::SCRIM(), 0);
    lv_obj_set_style_bg_opa(self->_themeOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(self->_themeOverlay, 0, 0);
    lv_obj_clear_flag(self->_themeOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(self->_themeOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(self->_themeOverlay, [](lv_event_t* ev) {
        DeviceSettingsScreen* s = (DeviceSettingsScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* ctx) { ((DeviceSettingsScreen*)ctx)->hideThemePicker(); }, s);
    }, LV_EVENT_CLICKED, self);

    self->_themeBtnm = lv_btnmatrix_create(lv_layer_top());
    lv_btnmatrix_set_map(self->_themeBtnm, g_themeMap.data());
#ifdef PLATFORM_TWATCH
    lv_coord_t rowH = 64;
#else
    lv_coord_t rowH = 26;
#endif
    lv_coord_t pickerH = count * rowH + 8;
    lv_coord_t maxH = Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT - 16;
    if (pickerH > maxH) pickerH = maxH;
    lv_obj_set_size(self->_themeBtnm, theme::MODAL_TEXT_WIDTH, pickerH);
    lv_obj_align(self->_themeBtnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(self->_themeBtnm, FONT_HEADING, 0);
    lv_obj_set_style_bg_color(self->_themeBtnm, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(self->_themeBtnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(self->_themeBtnm, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(self->_themeBtnm, 1, 0);
    lv_obj_set_style_radius(self->_themeBtnm, 8, 0);
    lv_obj_set_style_bg_color(self->_themeBtnm, theme::BG_INPUT(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(self->_themeBtnm, theme::TEXT_PRIMARY(), LV_PART_ITEMS);
    lv_obj_set_style_radius(self->_themeBtnm, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(self->_themeBtnm, theme::ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(self->_themeBtnm, theme::TEXT_ON_ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(self->_themeBtnm, [](lv_event_t* ev) {
        DeviceSettingsScreen* s = (DeviceSettingsScreen*)lv_event_get_user_data(ev);
        uint16_t idx = lv_btnmatrix_get_selected_btn(s->_themeBtnm);
        if (idx == LV_BTNMATRIX_BTN_NONE) return;

        if (idx < g_themeNames.size()) {
            auto& mgr = ConfigManager::instance();
            if (mgr.config().display.theme != g_themeNames[idx]) {
                mgr.config().display.theme = g_themeNames[idx];
                mgr.save();
                delay(200);
                ESP.restart();
                return;
            }
        }
        lv_async_call([](void* ctx) { ((DeviceSettingsScreen*)ctx)->hideThemePicker(); }, s);
    }, LV_EVENT_VALUE_CHANGED, self);

    UIManager::instance().switchToModalGroup(self->_themeBtnm);
}

void DeviceSettingsScreen::hideThemePicker() {
    if (!_themeBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    lv_obj_del_async(_themeBtnm);    _themeBtnm = nullptr;
    lv_obj_del_async(_themeOverlay); _themeOverlay = nullptr;
}

void DeviceSettingsScreen::hideNameEditor() {
    if (!_nameTextarea) return;
    UIManager::instance().restoreFromModalGroup();
#ifdef PLATFORM_TWATCH
    _nameKbd = nullptr;
#endif
    _nameTextarea = nullptr;
    lv_obj_del_async(_nameOverlay);
    _nameOverlay = nullptr;
    if (_screen) show();
}

void DeviceSettingsScreen::bootTextReadyCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_bootTextTextarea) return;
    const char* text = lv_textarea_get_text(self->_bootTextTextarea);
    String newBootText = text ? String(text) : String("");
    // Trim whitespace manually for native-build compatibility
    {
        const char* s = newBootText.c_str();
        int len = strlen(s);
        int l = 0, r = len - 1;
        while (l <= r && isspace((unsigned char)s[l])) ++l;
        while (r >= l && isspace((unsigned char)s[r])) --r;
        if (l > 0 || r < len - 1) {
            newBootText = newBootText.substring(l, r + 1);
        }
    }
    if (newBootText.length() > 32) newBootText = newBootText.substring(0, 32);
    auto& mgr = ConfigManager::instance();
    if (mgr.config().display.bootText != newBootText) {
        mgr.config().display.bootText = newBootText;
        mgr.save();
    }
    lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideBootTextEditor(); }, self);
}

void DeviceSettingsScreen::bootTextRowCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_bootTextTextarea) return;
    const auto& cfg = ConfigManager::instance().config();

    self->_bootTextOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_bootTextOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_bootTextOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_bootTextOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(self->_bootTextOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(self->_bootTextOverlay, 0, 0);
    lv_obj_clear_flag(self->_bootTextOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(self->_bootTextOverlay);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(lbl, t("lbl_boot_text"));
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    self->_bootTextTextarea = lv_textarea_create(self->_bootTextOverlay);
    lv_textarea_set_one_line(self->_bootTextTextarea, true);
    lv_textarea_set_max_length(self->_bootTextTextarea, 32);
    lv_textarea_set_placeholder_text(self->_bootTextTextarea, t("lbl_boot_text"));
    lv_textarea_set_text(self->_bootTextTextarea, cfg.display.bootText.c_str());
    lv_obj_set_width(self->_bootTextTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(self->_bootTextTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(self->_bootTextTextarea, theme::ACCENT(), LV_STATE_FOCUSED);

    // Button row
    lv_obj_t* btnRow = lv_obj_create(self->_bootTextOverlay);
    lv_obj_set_size(btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, theme::PAD_MEDIUM, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    // Save button
    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, bootTextReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    // Cancel button
    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<DeviceSettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideBootTextEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    lv_group_add_obj(g, self->_bootTextTextarea);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(self->_bootTextTextarea);
    UIManager::instance().switchToModalGroup(self->_bootTextOverlay);
    IInput::instance().attachToGroup(g);
    lv_obj_add_event_cb(self->_bootTextTextarea, bootTextReadyCb, LV_EVENT_READY, self);

#ifdef PLATFORM_TWATCH
    self->_bootTextKbd = lv_keyboard_create(self->_bootTextOverlay);
    lv_keyboard_set_textarea(self->_bootTextKbd, self->_bootTextTextarea);
    lv_keyboard_set_popovers(self->_bootTextKbd, true);
    lv_btnmatrix_set_btn_ctrl_all(self->_bootTextKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(self->_bootTextKbd, bootTextReadyCb, LV_EVENT_READY, self);
    lv_obj_add_event_cb(self->_bootTextKbd, [](lv_event_t* ev) {
        auto* self = static_cast<DeviceSettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_bootTextKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideBootTextEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

void DeviceSettingsScreen::hideBootTextEditor() {
    if (!_bootTextTextarea) return;
    UIManager::instance().restoreFromModalGroup();
#ifdef PLATFORM_TWATCH
    _bootTextKbd = nullptr;
#endif
    _bootTextTextarea = nullptr;
    lv_obj_del_async(_bootTextOverlay);
    _bootTextOverlay = nullptr;
}

void DeviceSettingsScreen::hideLanguagePicker() {
    if (!_langBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    lv_obj_del_async(_langBtnm);    _langBtnm = nullptr;
    lv_obj_del_async(_langOverlay); _langOverlay = nullptr;
}

void DeviceSettingsScreen::nameReadyCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_nameTextarea) return;
    const char* text = lv_textarea_get_text(self->_nameTextarea);
    String newName = text ? String(text) : String("");
    // Trim whitespace manually for native-build compatibility
    {
        const char* s = newName.c_str();
        int len = strlen(s);
        int l = 0, r = len - 1;
        while (l <= r && isspace((unsigned char)s[l])) ++l;
        while (r >= l && isspace((unsigned char)s[r])) --r;
        if (l > 0 || r < len - 1) {
            newName = newName.substring(l, r + 1);
        }
    }
    if (newName.length() > 20) newName = newName.substring(0, 20);
    auto& mgr = ConfigManager::instance();
    if (newName.length() > 0 && mgr.config().deviceName != newName) {
        mgr.config().deviceName = newName;
        mgr.save();
    }
    lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideNameEditor(); }, self);
}

void DeviceSettingsScreen::nameRowCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_nameTextarea) return;
    const auto& cfg = ConfigManager::instance().config();

    self->_nameOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_nameOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_nameOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_nameOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(self->_nameOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(self->_nameOverlay, 0, 0);
    lv_obj_clear_flag(self->_nameOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(self->_nameOverlay);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(lbl, t("lbl_device_name"));
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    self->_nameTextarea = lv_textarea_create(self->_nameOverlay);
    lv_textarea_set_one_line(self->_nameTextarea, true);
    lv_textarea_set_max_length(self->_nameTextarea, 20);
    lv_textarea_set_placeholder_text(self->_nameTextarea, t("lbl_device_name"));
    lv_textarea_set_text(self->_nameTextarea, cfg.deviceName.c_str());
    lv_obj_set_width(self->_nameTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(self->_nameTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(self->_nameTextarea, theme::ACCENT(), LV_STATE_FOCUSED);

    // Button row
    lv_obj_t* btnRow = lv_obj_create(self->_nameOverlay);
    lv_obj_set_size(btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, theme::PAD_MEDIUM, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    // Save button
    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, nameReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    // Cancel button
    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<DeviceSettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideNameEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    lv_group_add_obj(g, self->_nameTextarea);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(self->_nameTextarea);
    UIManager::instance().switchToModalGroup(self->_nameOverlay);
    IInput::instance().attachToGroup(g);
    lv_obj_add_event_cb(self->_nameTextarea, nameReadyCb, LV_EVENT_READY, self);

#ifdef PLATFORM_TWATCH
    self->_nameKbd = lv_keyboard_create(self->_nameOverlay);
    lv_keyboard_set_textarea(self->_nameKbd, self->_nameTextarea);
    lv_keyboard_set_popovers(self->_nameKbd, true);
    lv_btnmatrix_set_btn_ctrl_all(self->_nameKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(self->_nameKbd, nameReadyCb, LV_EVENT_READY, self);
    lv_obj_add_event_cb(self->_nameKbd, [](lv_event_t* ev) {
        auto* self = static_cast<DeviceSettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_nameKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((DeviceSettingsScreen*)p)->hideNameEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

// Language chooser state. File-scope so the static event callbacks can reach it:
namespace {
std::vector<String>      g_langCodes;
std::vector<String>      g_langLabels;
std::vector<const char*> g_langMap;
}  // namespace

void DeviceSettingsScreen::languageRowCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_langBtnm) return;

    g_langCodes.clear(); g_langLabels.clear(); g_langMap.clear();
    String avail = I18n::instance().availableLanguages();
    int start = 0;
    while (start < avail.length()) {
        int comma = -1;
        for (int i = start; i < avail.length(); ++i) {
            if (avail[i] == ',') { comma = i; break; }
        }
        if (comma < 0) comma = avail.length();
        String code = avail.substring(start, comma);
        // Trim whitespace manually for native-build compatibility
        {
            const char* s = code.c_str();
            int len = strlen(s);
            int l = 0, r = len - 1;
            while (l <= r && isspace((unsigned char)s[l])) ++l;
            while (r >= l && isspace((unsigned char)s[r])) --r;
            if (l > 0 || r < len - 1) {
                code = code.substring(l, r + 1);
            }
        }
        if (code.length() > 0) g_langCodes.push_back(code);
        start = comma + 1;
    }
    // Ensure "en" is present
    bool hasEn = false;
    for (const auto& c : g_langCodes) if (c == "en") { hasEn = true; break; }
    if (!hasEn) g_langCodes.insert(g_langCodes.begin(), String("en"));

    for (const auto& c : g_langCodes) {
        if (c == "en") g_langLabels.push_back("English");
        else g_langLabels.push_back(c);
    }
    g_langLabels.push_back(t("btn_cancel"));

    for (size_t i = 0; i < g_langLabels.size(); i++) {
        if (i > 0) g_langMap.push_back("\n");
        g_langMap.push_back(g_langLabels[i].c_str());
    }
    g_langMap.push_back("");
    int count = (int)g_langLabels.size();

    self->_langOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_langOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_langOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_langOverlay, theme::SCRIM(), 0);
    lv_obj_set_style_bg_opa(self->_langOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(self->_langOverlay, 0, 0);
    lv_obj_clear_flag(self->_langOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(self->_langOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(self->_langOverlay, [](lv_event_t* ev) {
        DeviceSettingsScreen* s = (DeviceSettingsScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* ctx) { ((DeviceSettingsScreen*)ctx)->hideLanguagePicker(); }, s);
    }, LV_EVENT_CLICKED, self);

    self->_langBtnm = lv_btnmatrix_create(lv_layer_top());
    lv_btnmatrix_set_map(self->_langBtnm, g_langMap.data());
#ifdef PLATFORM_TWATCH
    lv_coord_t rowH = 64;
#else
    lv_coord_t rowH = 26;
#endif
    lv_coord_t pickerH = count * rowH + 8;
    lv_coord_t maxH = Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT - 16;
    if (pickerH > maxH) pickerH = maxH;
    lv_obj_set_size(self->_langBtnm, theme::MODAL_TEXT_WIDTH, pickerH);
    lv_obj_align(self->_langBtnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(self->_langBtnm, FONT_HEADING, 0);
    lv_obj_set_style_bg_color(self->_langBtnm, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(self->_langBtnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(self->_langBtnm, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(self->_langBtnm, 1, 0);
    lv_obj_set_style_radius(self->_langBtnm, 8, 0);
    lv_obj_set_style_bg_color(self->_langBtnm, theme::BG_INPUT(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(self->_langBtnm, theme::TEXT_PRIMARY(), LV_PART_ITEMS);
    lv_obj_set_style_radius(self->_langBtnm, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(self->_langBtnm, theme::ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(self->_langBtnm, theme::TEXT_ON_ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(self->_langBtnm, [](lv_event_t* ev) {
        DeviceSettingsScreen* s = (DeviceSettingsScreen*)lv_event_get_user_data(ev);
        uint16_t idx = lv_btnmatrix_get_selected_btn(s->_langBtnm);
        if (idx == LV_BTNMATRIX_BTN_NONE) return;

        if (idx < g_langCodes.size()) {
            auto& mgr = ConfigManager::instance();
            String newLang = g_langCodes[idx];
            if (mgr.config().language != newLang) {
                mgr.config().language = newLang;
                mgr.save();
                delay(200);
                ESP.restart();
                return;
            }
        }
        lv_async_call([](void* ctx) { ((DeviceSettingsScreen*)ctx)->hideLanguagePicker(); }, s);
    }, LV_EVENT_VALUE_CHANGED, self);

    UIManager::instance().switchToModalGroup(self->_langBtnm);
}

void DeviceSettingsScreen::inlineSliderChangedCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);

    if (slider == self->_brightnessSlider) {
        lv_label_set_text(self->_brightnessValLbl, String(v).c_str());
        Display::instance().setBrightness((uint8_t)v);
    } else if (slider == self->_autoDimSlider) {
        String txt = v > 0 ? (String(v) + "s") : String(t("off"));
        lv_label_set_text(self->_autoDimValLbl, txt.c_str());
    } else if (slider == self->_dimBrightnessSlider) {
        String txt = v > 0 ? String(v) : String(t("off"));
        lv_label_set_text(self->_dimBrightnessValLbl, txt.c_str());
    } else if (slider == self->_kbdBrightnessSlider) {
        lv_label_set_text(self->_kbdBrightnessValLbl, String(v).c_str());
        IInput::instance().setBacklight((uint8_t)v);
    } else if (slider == self->_sosRepeatSlider) {
        lv_label_set_text(self->_sosRepeatValLbl, String(v).c_str());
    } else if (slider == self->_lowAlertSlider) {
        lv_label_set_text(self->_lowAlertValLbl, (String(v) + "%").c_str());
    }
}

void DeviceSettingsScreen::inlineSliderReleasedCb(lv_event_t* e) {
    DeviceSettingsScreen* self = (DeviceSettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);

    auto& mgr = ConfigManager::instance();
    if (slider == self->_brightnessSlider) {
        mgr.config().display.brightness = (uint8_t)v;
        mgr.save();
    } else if (slider == self->_autoDimSlider) {
        mgr.config().display.autoDimSeconds = (uint16_t)v;
        mgr.save();
    } else if (slider == self->_dimBrightnessSlider) {
        mgr.config().display.dimBrightness = (uint8_t)v;
        mgr.save();
    } else if (slider == self->_kbdBrightnessSlider) {
        mgr.config().display.kbdBrightness = (uint8_t)v;
        mgr.config().display.kbdBacklight = (v > 0);
        mgr.save();
    } else if (slider == self->_sosRepeatSlider) {
        mgr.config().sosRepeat = (uint8_t)v;
        mgr.save();
    } else if (slider == self->_lowAlertSlider) {
        mgr.config().battery.lowAlertThreshold = (uint8_t)v;
        mgr.save();
    }
}

void DeviceSettingsScreen::emojiToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    bool newVal = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.config().display.emoji = newVal;
    mgr.save();
}

void DeviceSettingsScreen::screenshotsToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    bool newVal = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.config().debug.screenshots = newVal;
    mgr.save();
}

}  // namespace mclite
