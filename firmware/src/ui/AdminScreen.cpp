#include "AdminScreen.h"
#include <Arduino.h>
#include "UIManager.h"
#include "theme.h"
#include "../config/ConfigManager.h"
#include "../hal/Display.h"
#include "../i18n/I18n.h"

namespace mclite {

void AdminScreen::create(lv_obj_t* parent) {
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
    lv_obj_t* title = lv_win_add_title(_screen, t("admin_title"));
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

void AdminScreen::show() {
    if (!_screen) return;

    lv_obj_clean(_content);

    // Group header
    auto addHeader = [this](const char* title) {
        lv_obj_t* lbl = lv_label_create(_content);
        lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
        lv_obj_set_style_text_color(lbl, theme::ACCENT(), 0);
        lv_obj_set_style_pad_top(lbl, theme::PAD_MEDIUM, 0);
        lv_label_set_text(lbl, title);
    };

    // Clickable link row: icon + label on the left, chevron on the right.
    auto addLink = [this](const char* icon, const char* label, lv_event_cb_t cb) {
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
        lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
        lv_label_set_text(lbl, (String(icon) + "  " + label).c_str());

        lv_obj_t* chev = lv_label_create(row);
        lv_obj_set_style_text_font(chev, FONT_BODY, 0);
        lv_obj_set_style_text_color(chev, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);

        lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, nullptr);
    };

    // ─── Companion ─── (hidden when permissions.companion is false)
    if (ConfigManager::instance().config().permissions.companion) {
        addHeader(t("grp_companion"));
        addLink(LV_SYMBOL_WIFI,      t("wifi_companion"), [](lv_event_t*) { UIManager::instance().showScreen(Screen::WIFI_SETUP); });
        addLink(LV_SYMBOL_USB,       t("usb_companion"),  [](lv_event_t*) { UIManager::instance().showScreen(Screen::USB_SETUP); });
        addLink(LV_SYMBOL_BLUETOOTH, t("ble_companion"),  [](lv_event_t*) { UIManager::instance().showScreen(Screen::BLE_SETUP); });
    }

    // ─── Conversations (read-only views on device; icons mirror the convo list) ───
    addHeader(t("grp_conversations"));
    addLink(ICON_DM,      t("sec_contacts_t"), [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Contacts); });
    addLink(ICON_CHANNEL, t("sec_channels_t"), [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Channels); });
    addLink(ICON_ROOM,    t("sec_rooms_t"),    [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Rooms); });
    if (ConfigManager::instance().config().messaging.cannedMessages) {
        addLink(LV_SYMBOL_LIST, t("sec_canned_messages_t"), [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::CannedMessages); });
    }

    // ─── Settings (gear icon on each) ───
    addHeader(t("grp_settings"));
    addLink(LV_SYMBOL_SETTINGS, t("sec_device"),    [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Device); });
    addLink(LV_SYMBOL_SETTINGS, t("sec_radio"),     [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Radio); });
    addLink(LV_SYMBOL_SETTINGS, t("sec_display"),   [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Display); });
    addLink(LV_SYMBOL_SETTINGS, t("sec_messaging"), [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Messaging); });
    addLink(LV_SYMBOL_SETTINGS, t("sec_sound"),     [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Sound); });
    addLink(LV_SYMBOL_SETTINGS, t("sec_gps"),       [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Gps); });
    addLink(LV_SYMBOL_SETTINGS, t("sec_battery"),   [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Battery); });
    addLink(LV_SYMBOL_SETTINGS, t("sec_security"),  [](lv_event_t*) { UIManager::instance().showSettings(SettingsSection::Security); });

    // ─── About: 3rd-party licenses (expandable; lives on the hub, not in Device) ───
    addHeader(t("sec_licenses"));
    {
        lv_obj_t* licToggle = lv_label_create(_content);
        lv_obj_set_style_text_font(licToggle, FONT_BODY, 0);
        lv_obj_set_style_text_color(licToggle, theme::ACCENT(), 0);
        lv_obj_add_flag(licToggle, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(licToggle, (String(LV_SYMBOL_RIGHT " ") + t("licenses_toggle")).c_str());

        lv_obj_t* licContainer = lv_obj_create(_content);
        lv_obj_set_size(licContainer, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(licContainer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(licContainer, 0, 0);
        lv_obj_set_style_pad_all(licContainer, 0, 0);
        lv_obj_set_style_pad_row(licContainer, 2, 0);
        lv_obj_set_flex_flow(licContainer, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(licContainer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(licContainer, LV_OBJ_FLAG_HIDDEN);

        static const char* licenseText =
            "LVGL 8.4.0 - MIT\n"
            "(c) 2021 LVGL Kft\n\n"
            "LovyanGFX 1.2.19 - MIT + BSD-2-Clause\n"
            "(c) lovyan03\n\n"
            "ArduinoJson 7.4.3 - MIT\n"
            "(c) 2014-2026 Benoit Blanchon\n\n"
            "RadioLib 7.3.0 - MIT\n"
            "(c) 2018 Jan Gromes\n\n"
            "MeshCore 1.10.0 - MIT\n"
            "(c) 2025 Scott Powell / rippleradios.com\n\n"
            "base64 1.4.0 - MIT\n"
            "(c) 2016 Densaugeo\n\n"
            "PNGdec 1.0.3 - Apache-2.0\n"
            "(c) 2020-2024 Larry Bank\n\n"
            "orlp/ed25519 - Zlib\n"
            "(c) 2015 Orson Peters\n\n"
            "Crypto 0.4.0 - MIT\n"
            "(c) 2015, 2018 Southern Storm Software\n\n"
            "RTClib 2.1.4 - MIT\n"
            "(c) 2019 Adafruit Industries\n\n"
            "Adafruit BusIO 1.17.4 - MIT\n"
            "(c) 2017 Adafruit Industries\n\n"
            "CayenneLPP 1.6.1 - MIT\n"
            "(c) Electronic Cats\n\n"
            "Melopero RV3028 1.2.0 - MIT\n"
            "(c) 2020 Melopero\n\n"
            "TinyGPSPlus 1.1.0 - LGPL-2.1\n"
            "(c) 2008-2024 Mikal Hart\n\n"
            "Arduino ESP32 core - LGPL-2.1\n"
            "(c) Espressif\n\n"
            "OpenMoji emoji font - CC-BY-SA 4.0\n"
            "(c) OpenMoji project (hfg-gmuend)\n\n"
            "MIT/Apache-2.0/Zlib libraries are used under\n"
            "the terms of those licenses. LGPL-2.1 libraries\n"
            "are dynamically linked; users may replace them\n"
            "by rebuilding with PlatformIO.\n\n"
            "Full license texts: see LICENSES.md";

        lv_obj_t* licLabel = lv_label_create(licContainer);
        lv_obj_set_style_text_font(licLabel, FONT_BODY, 0);
        lv_obj_set_style_text_color(licLabel, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_width(licLabel, LV_PCT(100));
        lv_label_set_long_mode(licLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text_static(licLabel, licenseText);

        lv_obj_add_event_cb(licToggle, [](lv_event_t* e) {
            lv_obj_t* toggle = lv_event_get_target(e);
            lv_obj_t* container = (lv_obj_t*)lv_event_get_user_data(e);
            bool hidden = lv_obj_has_flag(container, LV_OBJ_FLAG_HIDDEN);
            if (hidden) {
                lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(toggle, (String(LV_SYMBOL_DOWN " ") + t("licenses_toggle")).c_str());
            } else {
                lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(toggle, (String(LV_SYMBOL_RIGHT " ") + t("licenses_toggle")).c_str());
            }
        }, LV_EVENT_CLICKED, licContainer);
    }

    // Add content to input group so trackball can scroll
    lv_group_t* grp = lv_group_get_default();
    if (grp) {
        lv_group_add_obj(grp, _backBtn);
        lv_group_add_obj(grp, _content);
        lv_group_focus_obj(_content);
        lv_group_set_editing(grp, true);
    }

    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void AdminScreen::backBtnCb(lv_event_t* e) {
    UIManager::instance().goHome();
}

void AdminScreen::hide() {
    if (_screen) {
        lv_group_t* grp = lv_group_get_default();
        if (grp) {
            lv_group_set_editing(grp, false);
            lv_group_remove_obj(_backBtn);
            lv_group_remove_obj(_content);
        }
        lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    }
}

void AdminScreen::tick() {
    // Hub is static — nothing to refresh.
}

}  // namespace mclite
