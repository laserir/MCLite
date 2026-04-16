#include "StatusBar.h"
#include "theme.h"
#include "../hal/Battery.h"
#include "../hal/GPS.h"
#include "../hal/Speaker.h"
#include "../config/ConfigManager.h"
#include "../util/TimeHelper.h"

namespace mclite {

void StatusBar::create(lv_obj_t* parent) {
    _bar = lv_obj_create(parent);
    lv_obj_set_size(_bar, 320, theme::STATUS_BAR_HEIGHT);
    lv_obj_align(_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(_bar, theme::BG_STATUS_BAR, 0);
    lv_obj_set_style_bg_opa(_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_bar, 0, 0);
    lv_obj_set_style_radius(_bar, 0, 0);
    lv_obj_set_style_pad_all(_bar, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_bar, theme::PAD_SMALL, 0);

    // Device name (left, takes remaining space)
    _lblName = lv_label_create(_bar);
    lv_obj_set_style_text_font(_lblName, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_lblName, theme::TEXT_PRIMARY, 0);
    lv_obj_set_flex_grow(_lblName, 1);

    const auto& cfg = ConfigManager::instance().config();
    lv_label_set_text(_lblName, cfg.deviceName.c_str());

    // --- Everything below is right-aligned (no flex_grow) ---

    // Key lock indicator (hidden by default, leftmost of right-side icons)
    _keyLockIcon = lv_label_create(_bar);
    lv_obj_set_style_text_font(_keyLockIcon, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_keyLockIcon, theme::GPS_LAST_KNOWN, 0);
    lv_label_set_text(_keyLockIcon, ICON_KEY_LOCK);
    lv_obj_add_flag(_keyLockIcon, LV_OBJ_FLAG_HIDDEN);

    // Sound icon — clickable label, larger font for tap target
    _soundIcon = lv_label_create(_bar);
    lv_obj_set_style_text_font(_soundIcon, FONT_NORMAL, 0);
    lv_obj_add_flag(_soundIcon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(_soundIcon, 8);  // Expand touch target
    lv_obj_add_event_cb(_soundIcon, soundClickCb, LV_EVENT_CLICKED, this);

    // GPS indicator
    _gpsIcon = lv_label_create(_bar);
    lv_obj_set_style_text_font(_gpsIcon, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_gpsIcon, theme::TEXT_SECONDARY, 0);

    // Battery
    _lblBatt = lv_label_create(_bar);
    lv_obj_set_style_text_font(_lblBatt, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_lblBatt, theme::TEXT_PRIMARY, 0);

    // Clock
    _lblTime = lv_label_create(_bar);
    lv_obj_set_style_text_font(_lblTime, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_lblTime, theme::TEXT_PRIMARY, 0);

    updateSoundIcon();
    update();
}

void StatusBar::soundClickCb(lv_event_t* e) {
    StatusBar* self = (StatusBar*)lv_event_get_user_data(e);
    Speaker::instance().toggleMute();
    self->updateSoundIcon();
}

void StatusBar::updateSoundIcon() {
    if (!_soundIcon) return;
    bool muted = Speaker::instance().isMuted();
    lv_label_set_text(_soundIcon, muted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(_soundIcon,
        muted ? theme::TEXT_SECONDARY : theme::TEXT_PRIMARY, 0);
}

void StatusBar::update() {
    const auto& cfg = ConfigManager::instance().config();

    // Device name (may change after first-boot generation)
    lv_label_set_text(_lblName, cfg.deviceName.c_str());

    // Battery — icon only
    auto& batt = Battery::instance();
    batt.update();
    uint8_t pct = batt.percent();
    const char* battIcon;
    if (pct > 80)      battIcon = LV_SYMBOL_BATTERY_FULL;
    else if (pct > 60) battIcon = LV_SYMBOL_BATTERY_3;
    else if (pct > 40) battIcon = LV_SYMBOL_BATTERY_2;
    else if (pct > 20) battIcon = LV_SYMBOL_BATTERY_1;
    else                battIcon = LV_SYMBOL_BATTERY_EMPTY;

    if (batt.isCharging()) {
        static char battBuf[16];
        snprintf(battBuf, sizeof(battBuf), "%s " LV_SYMBOL_CHARGE, battIcon);
        lv_label_set_text(_lblBatt, battBuf);
    } else {
        lv_label_set_text(_lblBatt, battIcon);
    }
    lv_obj_set_style_text_color(_lblBatt,
        pct <= 20 ? theme::BATTERY_LOW : theme::TEXT_PRIMARY, 0);

    // Clock — show HH:MM in local time (auto-DST via POSIX TZ)
    auto& gps = GPS::instance();
    if (gps.isTimeSynced() && gps.hasTime()) {
        char timeStr[8];
        TimeHelper::instance().formatHHMM(gps.currentTimestamp(), timeStr, sizeof(timeStr));
        lv_label_set_text(_lblTime, timeStr[0] ? timeStr : "");
    } else {
        lv_label_set_text(_lblTime, "");
    }

    // GPS indicator — green=live, amber=last known, gray=no fix
    if (!cfg.gpsEnabled) {
        lv_label_set_text(_gpsIcon, "");
    } else {
        lv_label_set_text(_gpsIcon, LV_SYMBOL_GPS);
        switch (gps.fixStatus()) {
            case FixStatus::LIVE:
                lv_obj_set_style_text_color(_gpsIcon, theme::ONLINE_DOT, 0);
                break;
            case FixStatus::LAST_KNOWN:
                lv_obj_set_style_text_color(_gpsIcon, theme::GPS_LAST_KNOWN, 0);
                break;
            case FixStatus::NO_FIX:
                lv_obj_set_style_text_color(_gpsIcon, theme::TEXT_SECONDARY, 0);
                break;
        }
    }
}

void StatusBar::setKeyLocked(bool locked) {
    if (!_keyLockIcon) return;
    if (locked) {
        lv_obj_clear_flag(_keyLockIcon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_keyLockIcon, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace mclite
