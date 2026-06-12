#include "StatusBar.h"
#include "theme.h"
#include "UIManager.h"
#include "../hal/Battery.h"
#include "../hal/Display.h"
#include "../hal/GPS.h"
#include "../hal/Speaker.h"
#include "../config/ConfigManager.h"
#include "../util/TimeHelper.h"
#include "../net/WiFiManager.h"
#include "../companion/CompanionService.h"

namespace mclite {

void StatusBar::create(lv_obj_t* parent) {
    _bar = lv_obj_create(parent);
    lv_obj_set_size(_bar, Display::width(), theme::STATUS_BAR_HEIGHT);
    lv_obj_align(_bar, LV_ALIGN_TOP_MID, 0, theme::SAFE_AREA_TOP);
    lv_obj_set_style_bg_color(_bar, theme::BG_STATUS_BAR, 0);
    lv_obj_set_style_bg_opa(_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_bar, 0, 0);
    lv_obj_set_style_radius(_bar, 0, 0);
    lv_obj_set_style_pad_all(_bar, theme::PAD_SMALL, 0);
    // Inner horizontal padding — full-width bg, content inset from edges.
    lv_obj_set_style_pad_hor(_bar, theme::STATUS_BAR_PAD_HOR, 0);
    lv_obj_clear_flag(_bar, LV_OBJ_FLAG_SCROLLABLE);

#ifdef PLATFORM_TWATCH
    // T-Watch: status bar = row 1 (device name centered) + row 2 (sound /
    // GPS / battery centered, 28-pt). Clock lives in the separate footer
    // bar at the bottom of the screen (created below). Status bar height
    // (96) absorbs the rounded-top safe area; pad_top keeps content out
    // of the corner.
    lv_obj_set_style_pad_top(_bar, theme::STATUS_BAR_PAD_V, 0);
    lv_obj_set_flex_flow(_bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_bar, theme::PAD_SMALL, 0);

    auto styleRow = [](lv_obj_t* row) {
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    };

    _nameRow = lv_obj_create(_bar);
    styleRow(_nameRow);
    lv_obj_set_style_pad_column(_nameRow, theme::PAD_SMALL, 0);

    _iconRow = lv_obj_create(_bar);
    styleRow(_iconRow);
    lv_obj_set_style_pad_column(_iconRow, theme::STATUS_BAR_ICON_GAP, 0);

    // OFFGRID indicator — inline with name on row 1.
    _lblOffgrid = lv_label_create(_nameRow);
    lv_label_set_text(_lblOffgrid, "OFFGRID");
    lv_obj_set_style_text_font(_lblOffgrid, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_lblOffgrid, theme::OFFGRID_ACCENT, 0);
    lv_obj_add_flag(_lblOffgrid, LV_OBJ_FLAG_HIDDEN);

    // Device name — content-sized so the row centers it.
    _lblName = lv_label_create(_nameRow);
    lv_obj_set_style_text_font(_lblName, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_lblName, theme::TEXT_PRIMARY, 0);
    lv_label_set_long_mode(_lblName, LV_LABEL_LONG_DOT);

    const auto& cfg = ConfigManager::instance().config();
    lv_label_set_text(_lblName, cfg.deviceName.c_str());

    // Row 2 icons — sound / GPS / battery only. Clock moved to footer.
    _soundIcon = lv_label_create(_iconRow);
    lv_obj_set_style_text_font(_soundIcon, FONT_STATUSBAR_ICON, 0);
    lv_obj_add_flag(_soundIcon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(_soundIcon, 8);
    lv_obj_add_event_cb(_soundIcon, soundClickCb, LV_EVENT_CLICKED, this);

    _gpsIcon = lv_label_create(_iconRow);
    lv_obj_set_style_text_font(_gpsIcon, FONT_STATUSBAR_ICON, 0);
    lv_obj_set_style_text_color(_gpsIcon, theme::TEXT_SECONDARY, 0);
    lv_obj_add_flag(_gpsIcon, LV_OBJ_FLAG_CLICKABLE);   // tap → general map
    lv_obj_set_ext_click_area(_gpsIcon, 8);
    lv_obj_add_event_cb(_gpsIcon, gpsClickCb, LV_EVENT_CLICKED, this);

    _wifiIcon = lv_label_create(_iconRow);  // between GPS and battery
    lv_obj_set_style_text_font(_wifiIcon, FONT_STATUSBAR_ICON, 0);
    lv_obj_set_style_text_color(_wifiIcon, theme::ACCENT, 0);
    lv_label_set_text(_wifiIcon, LV_SYMBOL_WIFI);
    lv_obj_add_flag(_wifiIcon, LV_OBJ_FLAG_HIDDEN);

    _bleIcon = lv_label_create(_iconRow);
    lv_obj_set_style_text_font(_bleIcon, FONT_STATUSBAR_ICON, 0);
    lv_obj_set_style_text_color(_bleIcon, theme::ACCENT, 0);
    lv_label_set_text(_bleIcon, LV_SYMBOL_BLUETOOTH);
    lv_obj_add_flag(_bleIcon, LV_OBJ_FLAG_HIDDEN);

    _lblBatt = lv_label_create(_iconRow);
    lv_label_set_recolor(_lblBatt, true);   // recolor the charge bolt independently
    lv_obj_set_style_text_font(_lblBatt, FONT_STATUSBAR_ICON, 0);
    lv_obj_set_style_text_color(_lblBatt, theme::TEXT_PRIMARY, 0);

    // Footer bar — bottom of the screen, mirrors the status bar.
    // Absorbs the rounded-bottom safe area via FOOTER_PAD_V.
    _footer = lv_obj_create(parent);
    lv_obj_set_size(_footer, Display::width(), theme::FOOTER_HEIGHT);
    lv_obj_align(_footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_footer, theme::BG_STATUS_BAR, 0);
    lv_obj_set_style_bg_opa(_footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_footer, 0, 0);
    lv_obj_set_style_radius(_footer, 0, 0);
    lv_obj_set_style_pad_top(_footer, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_bottom(_footer, theme::FOOTER_PAD_V, 0);
    lv_obj_set_style_pad_hor(_footer, theme::STATUS_BAR_PAD_HOR, 0);
    lv_obj_clear_flag(_footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _lblTime = lv_label_create(_footer);
    lv_obj_set_style_text_font(_lblTime, FONT_STATUSBAR_ICON, 0);
    lv_obj_set_style_text_color(_lblTime, theme::TEXT_PRIMARY, 0);
#else
    // T-Deck: single flex-row, device name left (grow), icons right.
    lv_obj_set_flex_flow(_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_bar, theme::PAD_SMALL, 0);

    // OFFGRID indicator — created FIRST so flex order places it leftmost.
    // Hidden by default; update() toggles visibility from cfg.offgrid.enabled.
    _lblOffgrid = lv_label_create(_bar);
    lv_label_set_text(_lblOffgrid, "OFFGRID");
    lv_obj_set_style_text_font(_lblOffgrid, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_lblOffgrid, theme::OFFGRID_ACCENT, 0);
    lv_obj_add_flag(_lblOffgrid, LV_OBJ_FLAG_HIDDEN);

    // Device name (left, takes remaining space). LONG_DOT so long names truncate
    // instead of wrapping when the OFFGRID label shares the left side.
    _lblName = lv_label_create(_bar);
    lv_obj_set_style_text_font(_lblName, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_lblName, theme::TEXT_PRIMARY, 0);
    lv_label_set_long_mode(_lblName, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(_lblName, 1);

    const auto& cfg = ConfigManager::instance().config();
    lv_label_set_text(_lblName, cfg.deviceName.c_str());

    // --- Everything below is right-aligned (no flex_grow) ---

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
    lv_obj_add_flag(_gpsIcon, LV_OBJ_FLAG_CLICKABLE);   // tap → general map
    lv_obj_set_ext_click_area(_gpsIcon, 8);
    lv_obj_add_event_cb(_gpsIcon, gpsClickCb, LV_EVENT_CLICKED, this);

    // WiFi indicator (between GPS and battery) — shown only while connected
    _wifiIcon = lv_label_create(_bar);
    lv_obj_set_style_text_font(_wifiIcon, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_wifiIcon, theme::ACCENT, 0);
    lv_label_set_text(_wifiIcon, LV_SYMBOL_WIFI);
    lv_obj_add_flag(_wifiIcon, LV_OBJ_FLAG_HIDDEN);

    _bleIcon = lv_label_create(_bar);
    lv_obj_set_style_text_font(_bleIcon, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_bleIcon, theme::ACCENT, 0);
    lv_label_set_text(_bleIcon, LV_SYMBOL_BLUETOOTH);
    lv_obj_add_flag(_bleIcon, LV_OBJ_FLAG_HIDDEN);

    // Battery
    _lblBatt = lv_label_create(_bar);
    lv_label_set_recolor(_lblBatt, true);   // recolor the charge bolt independently
    lv_obj_set_style_text_font(_lblBatt, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_lblBatt, theme::TEXT_PRIMARY, 0);

    // Clock
    _lblTime = lv_label_create(_bar);
    lv_obj_set_style_text_font(_lblTime, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_lblTime, theme::TEXT_PRIMARY, 0);
#endif

    updateSoundIcon();
    update();
}

void StatusBar::soundClickCb(lv_event_t* e) {
    StatusBar* self = (StatusBar*)lv_event_get_user_data(e);
    Speaker::instance().toggleVolume();
    self->updateSoundIcon();
}

void StatusBar::gpsClickCb(lv_event_t* e) {
    UIManager::instance().showGeneralMap();
}

void StatusBar::updateSoundIcon() {
    if (!_soundIcon) return;
    bool muted = Speaker::instance().isMuted();

    if (muted) {
        lv_label_set_text(_soundIcon, LV_SYMBOL_MUTE);
    } else {
        bool isMid = Speaker::instance().isVolumeMid();
        lv_label_set_text(_soundIcon, isMid ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX);
    }
    lv_obj_set_style_text_color(_soundIcon,
        muted ? theme::TEXT_SECONDARY : theme::TEXT_PRIMARY, 0);
}

void StatusBar::update() {
    const auto& cfg = ConfigManager::instance().config();

    // OFFGRID indicator — show whenever offgrid mode is active on this boot
    if (_lblOffgrid) {
        if (cfg.offgrid.enabled) lv_obj_clear_flag(_lblOffgrid, LV_OBJ_FLAG_HIDDEN);
        else                     lv_obj_add_flag(_lblOffgrid, LV_OBJ_FLAG_HIDDEN);
    }

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

    // A USB companion client actively bridging → tint just the charge bolt green
    // (recolor markup); the battery glyph keeps its normal/low color.
    auto& comp = CompanionService::instance();
    bool usbBridging = comp.usbCompanionEnabled() && comp.clientConnected();
    if (batt.isCharging()) {
        static char battBuf[32];
        if (usbBridging)
            snprintf(battBuf, sizeof(battBuf), "%s #00cc66 " LV_SYMBOL_CHARGE "#", battIcon);
        else
            snprintf(battBuf, sizeof(battBuf), "%s " LV_SYMBOL_CHARGE, battIcon);
        lv_label_set_text(_lblBatt, battBuf);
    } else {
        lv_label_set_text(_lblBatt, battIcon);
    }
    lv_obj_set_style_text_color(_lblBatt,
        pct <= 20 ? theme::BATTERY_LOW : theme::TEXT_PRIMARY, 0);

    // WiFi icon — visible only while connected. Green when a companion client is
    // attached (actively bridging), blue when connected for WiFi only.
    if (_wifiIcon) {
        if (WiFiManager::instance().isConnected()) {
            lv_obj_clear_flag(_wifiIcon, LV_OBJ_FLAG_HIDDEN);
            // Green only when the WiFi transport is the one bridging a client
            // (not when USB companion is active).
            bool wifiBridging = comp.wifiCompanionEnabled() && comp.clientConnected();
            lv_obj_set_style_text_color(_wifiIcon, wifiBridging ? theme::ONLINE_DOT : theme::ACCENT, 0);
        } else {
            lv_obj_add_flag(_wifiIcon, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // BLE icon — visible while BLE companion is active (advertising/connected).
    // Green once a client is connected, blue while just advertising.
    if (_bleIcon) {
        if (comp.bleCompanionEnabled()) {
            lv_obj_clear_flag(_bleIcon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(_bleIcon,
                comp.clientConnected() ? theme::ONLINE_DOT : theme::ACCENT, 0);
        } else {
            lv_obj_add_flag(_bleIcon, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Clock — show HH:MM in local time (auto-DST via POSIX TZ). Prefer GPS
    // when locked; fall back to the system clock (which is seeded from the
    // RTC at boot on T-Watch) so we still display a sensible time during
    // GPS-cold periods.
    auto& gps = GPS::instance();
    uint32_t clockEpoch = 0;
    if (gps.isTimeSynced() && gps.hasTime()) {
        clockEpoch = gps.currentTimestamp();
    } else {
        clockEpoch = TimeHelper::instance().nowEpoch();
    }
    if (clockEpoch) {
        char timeStr[8];
        TimeHelper::instance().formatHHMM(clockEpoch, timeStr, sizeof(timeStr));
        lv_label_set_text(_lblTime, timeStr[0] ? timeStr : "");
    } else {
        lv_label_set_text(_lblTime, "");
    }

    // GPS indicator — green=live, amber=last known, gray=no fix
    if (!cfg.gpsEnabled) {
        // GPS disabled in config: keep the icon present but dimmed so the
        // general map stays reachable (tap still opens it — the map can center
        // on heard nodes' locations even without our own fix).
        lv_label_set_text(_gpsIcon, LV_SYMBOL_GPS);
        lv_obj_set_style_text_color(_gpsIcon, theme::TEXT_TIMESTAMP, 0);
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

}  // namespace mclite
