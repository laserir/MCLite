#include "SettingsScreen.h"
#include <Arduino.h>
#include <time.h>
#include <vector>
#include "UIManager.h"
#include "theme.h"
#include "../config/ConfigManager.h"
#include "../config/defaults.h"
#include "../config/radio_presets.h"
#include "../hal/Display.h"
#include "../hal/IInput.h"
#include "../hal/Speaker.h"
#include "../hal/Battery.h"
#include "../hal/GPS.h"
#include "../mesh/MeshManager.h"
#include "../mesh/ContactStore.h"
#include "../mesh/ChannelStore.h"
#include "../storage/HeardAdvertCache.h"
#include "../storage/MessageStore.h"
#include "../util/TimeHelper.h"
#include "../util/offgrid.h"
#include "../util/locprecision.h"
#include "../i18n/I18n.h"
#include "ModalDialog.h"
#include <esp_random.h>

namespace mclite {

namespace {
// Batched-save state (A+B model): editors update the in-memory config live and
// set g_dsDirty; the SD write happens once when leaving the screen (hide()).
// Theme/language/radio also set g_dsReboot so we reboot once on leave to apply.
bool g_dsDirty  = false;
bool g_dsReboot = false;

// i18n key for a section's window title.
const char* sectionTitleKey(SettingsSection s) {
    switch (s) {
        case SettingsSection::Device:    return "sec_device";
        case SettingsSection::Radio:     return "sec_radio";
        case SettingsSection::Display:   return "sec_display";
        case SettingsSection::Messaging: return "sec_messaging";
        case SettingsSection::Sound:     return "sec_sound";
        case SettingsSection::Gps:       return "sec_gps";
        case SettingsSection::Battery:   return "sec_battery";
        case SettingsSection::Security:  return "sec_security";
        case SettingsSection::Contacts:      return "sec_contacts_t";
        case SettingsSection::Channels:      return "sec_channels_t";
        case SettingsSection::Rooms:         return "sec_rooms_t";
        case SettingsSection::CannedMessages:return "sec_canned_messages_t";
    }
    return "sec_device";
}

// Friendly name for a theme: translated label for built-ins, raw name for custom.
String themeDisplayName(const String& name) {
    if (name == "dark")          return t("theme_dark");
    if (name == "light")         return t("theme_light");
    if (name == "amber")         return t("theme_amber");
    if (name == "high_contrast") return t("theme_high_contrast");
    return name;  // custom theme — show its own name
}

// Friendly label for a location-advert precision value (mirrors the config
// tool's precision dropdown). Shared by the GPS read-only row and the picker.
String locPrecisionLabel(uint8_t p) {
    if (p == 0)       return t("off");
    if (p >= 32)      return t("loc_exact");
    if (p == 19)      return "~100 m";
    if (p == 16)      return "~750 m";
    if (p == 14)      return "~3 km";
    if (p == 12)      return "~12 km";
    if (p == 10)      return "~50 km";
    uint32_t m = locPrecisionMeters(p);
    return (m >= 1000) ? ("~" + String(m / 1000) + " km") : ("~" + String(m) + " m");
}

// Advert-interval options (minutes) + their compact labels, shared by the Radio
// nav-row display and the picker. (Off, 1h, 3h, 6h, 12h, 24h, 7d.)
const uint16_t ADVERT_MINS[] = {0, 60, 180, 360, 720, 1440, 10080};
String advertLabel(uint16_t m) {
    if (m == 0)      return t("off");
    if (m == 10080)  return "7d";
    if (m % 60 == 0) return String(m / 60) + "h";
    return String(m) + "m";
}
}  // namespace

void SettingsScreen::create(lv_obj_t* parent) {
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
    _titleLabel = lv_win_add_title(_screen, t("device_settings_title"));  // set per section in show()
    lv_obj_set_style_text_font(_titleLabel, FONT_HEADING, 0);
    lv_obj_set_style_text_color(_titleLabel, theme::TEXT_PRIMARY(), 0);

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

// ─────────────────────────── Row helpers ───────────────────────────

lv_obj_t* SettingsScreen::createRowContainer(lv_obj_t* parent) {
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

void SettingsScreen::addSectionHeader(const char* title) {
    lv_obj_t* lbl = lv_label_create(_content);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::ACCENT(), 0);
    lv_obj_set_style_pad_top(lbl, theme::PAD_MEDIUM, 0);
    lv_label_set_text(lbl, title);
}

lv_obj_t* SettingsScreen::addReadOnlyRow(const char* label, const String& value) {
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

    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
    lv_label_set_text(lbl, label);

    lv_obj_t* val = lv_label_create(row);
    lv_obj_set_style_text_font(val, FONT_BODY, 0);
    lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(val, value.c_str());
    return row;
}

lv_obj_t* SettingsScreen::addNavRow(const char* label, const String& value, lv_event_cb_t cb) {
    lv_obj_t* row = createRowContainer(_content);

    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
    lv_label_set_text(lbl, label);

    lv_obj_t* val = lv_label_create(row);
    lv_obj_set_style_text_font(val, FONT_BODY, 0);
    lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(val, (value + "  " LV_SYMBOL_RIGHT).c_str());

    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, this);
    return row;
}

void SettingsScreen::addSwitchRow(const char* label, bool checked, lv_event_cb_t cb,
                                  void* swUserData, bool rowClickable) {
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
    if (rowClickable) lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
    lv_label_set_text(lbl, label);

    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_set_style_bg_color(sw, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    if (swUserData) lv_obj_set_user_data(sw, swUserData);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, this);
}

lv_obj_t* SettingsScreen::addSliderRow(const char* label, lv_obj_t** sliderOut, lv_obj_t** valLblOut,
                                       int32_t lo, int32_t hi, int32_t val, const String& valText) {
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
    lv_label_set_text(lbl, label);

    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_set_width(sl, LV_PCT(40));
    lv_slider_set_range(sl, lo, hi);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, theme::ACCENT(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, theme::ACCENT(), LV_PART_KNOB);
    lv_obj_add_event_cb(sl, inlineSliderChangedCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(sl, inlineSliderReleasedCb, LV_EVENT_RELEASED, this);
    *sliderOut = sl;

    lv_obj_t* vlbl = lv_label_create(row);
    lv_obj_set_style_text_font(vlbl, FONT_BODY, 0);
    lv_obj_set_style_text_color(vlbl, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_width(vlbl, LV_PCT(10));
    lv_label_set_text(vlbl, valText.c_str());
    *valLblOut = vlbl;
    return row;
}

bool SettingsScreen::canEdit(bool basic) const {
    const String& m = ConfigManager::instance().config().permissions.settings;
    if (m == "none")       return false;
    if (m == "restricted") return basic;
    return true;  // "full" (or unknown — fail open to the existing behaviour)
}

void SettingsScreen::addNavRowGated(const char* label, const String& value, lv_event_cb_t cb, bool basic) {
    if (canEdit(basic)) addNavRow(label, value, cb);
    else                addReadOnlyRow(label, value);
}

void SettingsScreen::addSwitchRowGated(const char* label, bool checked, lv_event_cb_t cb, void* ud, bool basic) {
    if (canEdit(basic)) addSwitchRow(label, checked, cb, ud);
    else                addReadOnlyRow(label, checked ? t("on") : t("off"));
}

void SettingsScreen::addSliderRowGated(const char* label, lv_obj_t** sliderOut, lv_obj_t** valLblOut,
                                       int32_t lo, int32_t hi, int32_t val, const String& valText, bool basic) {
    if (canEdit(basic)) addSliderRow(label, sliderOut, valLblOut, lo, hi, val, valText);
    else                addReadOnlyRow(label, valText);
}

// ─────────────────────────── show / dispatch ───────────────────────────

void SettingsScreen::show() {
    if (!_screen) return;

    // Preserve the scroll position across a rebuild. show() doubles as the
    // post-edit refresh (editors call it on close); without this the clean +
    // re-focus of _content jumps the view. Restored at the end, after focus.
    lv_coord_t scrollY = lv_obj_get_scroll_y(_content);

    lv_obj_clean(_content);

    // Stale-pointer guard: a section rebuild only creates the sliders for that
    // section, so clear every slider handle first. The inline slider callbacks
    // match the event target by pointer — a leftover handle from another section
    // points at a freed object whose address could be reused, which would
    // misroute the matched slider to the wrong config field. Nulling here means
    // only the current section's freshly-set handles can ever match.
    _brightnessSlider = _autoDimSlider = _dimBrightnessSlider = _kbdBrightnessSlider =
        _sosRepeatSlider = _lowAlertSlider = _txPowerSlider =
        _maxHistorySlider = _maxRetriesSlider = _clockOffsetSlider = _lastKnownSlider = nullptr;
    _heardCountLabel = nullptr;

    // Window title reflects the current section.
    if (_titleLabel) lv_label_set_text(_titleLabel, t(sectionTitleKey(_section)));

    switch (_section) {
        case SettingsSection::Device:    buildDevice();    break;
        case SettingsSection::Radio:     buildRadio();     break;
        case SettingsSection::Display:   buildDisplay();   break;
        case SettingsSection::Messaging: buildMessaging(); break;
        case SettingsSection::Sound:     buildSound();     break;
        case SettingsSection::Gps:       buildGps();       break;
        case SettingsSection::Battery:   buildBattery();   break;
        case SettingsSection::Security:  buildSecurity();  break;
        case SettingsSection::Contacts:
        case SettingsSection::Channels:
        case SettingsSection::Rooms:         buildConvoList();   break;
        case SettingsSection::CannedMessages:buildCannedList();  break;
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

    // Restore the pre-rebuild scroll position (after focus, so it wins).
    lv_obj_scroll_to_y(_content, scrollY, LV_ANIM_OFF);

    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

// ─────────────────────────── section builders ───────────────────────────

void SettingsScreen::buildDevice() {
    const auto& cfg = ConfigManager::instance().config();

    addNavRowGated(t("lbl_device_name"), cfg.deviceName, nameRowCb, false);
    addNavRowGated(t("lbl_language"), cfg.language.isEmpty() ? String("English") : cfg.language, languageRowCb, false);
    addNavRowGated(t("lbl_boot_text"),
                   cfg.display.bootText.length() > 0 ? cfg.display.bootText : String(t("off")),
                   bootTextRowCb, false);

    // Read-only diagnostics (moved off Admin).
    addReadOnlyRow(t("lbl_firmware"), String("MCLite v") + defaults::FIRMWARE_VERSION);
    addReadOnlyRow(t("lbl_vendor"), defaults::FIRMWARE_VENDOR);
    addReadOnlyRow(t("lbl_built"), String(__DATE__) + " " + __TIME__);
    if (cfg.publicKey.length() > 0) {
        addReadOnlyRow(t("lbl_public_key"), cfg.publicKey.substring(0, 16) + "...");
    }
    addReadOnlyRow(t("lang_available"), I18n::instance().availableLanguages());
    // 3rd-party licenses live on the Admin hub (About), not here.
}

void SettingsScreen::buildRadio() {
    const auto& cfg = ConfigManager::instance().config();

    // Offgrid toggle — opens a reboot-confirm dialog (moved off Admin).
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::OFFGRID_ACCENT(), 0);
        lv_obj_set_style_bg_opa(row, cfg.offgrid.enabled ? LV_OPA_50 : LV_OPA_20, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        // CLICKABLE added below only when editable (full settings access).

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
        lv_label_set_text(lbl, t("lbl_offgrid"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        if (cfg.offgrid.enabled) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%s (%d MHz)", t("offgrid_on"),
                     (int)mclite::offgridFreqFor(cfg.radio.frequency));
            lv_label_set_text(val, buf);
        } else {
            lv_label_set_text(val, t("offgrid_off"));
        }
        // Read-only (not clickable) unless full settings access.
        if (canEdit(false)) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, offgridRowCb, LV_EVENT_CLICKED, this);
        }
    }

    // Heard adverts — live count, opens the list. _heardCountLabel refreshed by tick().
    {
        lv_obj_t* row = createRowContainer(_content);
        _heardCountLabel = lv_label_create(row);
        lv_obj_set_style_text_font(_heardCountLabel, FONT_BODY, 0);
        lv_obj_set_style_text_color(_heardCountLabel, theme::TEXT_PRIMARY(), 0);
        char rowBuf[40];
        snprintf(rowBuf, sizeof(rowBuf), "%s (%d)",
                 t("heard_adverts_title"), HeardAdvertCache::instance().count());
        lv_label_set_text(_heardCountLabel, rowBuf);
        _heardCacheVersion = HeardAdvertCache::instance().version();

        lv_obj_t* chev = lv_label_create(row);
        lv_obj_set_style_text_font(chev, FONT_BODY, 0);
        lv_obj_set_style_text_color(chev, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);

        lv_obj_add_event_cb(row, heardAdvertsRowCb, LV_EVENT_CLICKED, this);
    }

    // Region preset picker — applies a freq/SF/BW/CR bundle (reboot on leave).
    int pi = matchRadioPreset(cfg.radio);
    String presetVal = (pi >= 0) ? String(RADIO_PRESETS[pi].label) : String(t("preset_custom"));
    addNavRowGated(t("lbl_region_preset"), presetVal, regionRowCb, false);

    // TX power slider (reboot on leave).
    addSliderRowGated(t("lbl_tx_power"), &_txPowerSlider, &_txPowerValLbl,
                      0, 22, cfg.radio.txPower, String(cfg.radio.txPower) + " dBm", false);

    // Advert interval picker (0 = off; reboot on leave).
    addNavRowGated(t("lbl_advert_interval"), advertLabel(cfg.radio.advertIntervalMin), advertRowCb, false);

    // Read-only diagnostics.
    {
        float activeFreq = cfg.radio.frequency;
        String freqSuffix = " MHz";
        if (cfg.offgrid.enabled) {
            activeFreq = mclite::offgridFreqFor(cfg.radio.frequency);
            freqSuffix += " (offgrid)";
        }
        addReadOnlyRow(t("lbl_frequency"), String(activeFreq, 3) + freqSuffix);
    }
    addReadOnlyRow(t("lbl_sf_bw"),
                   String(cfg.radio.spreadingFactor) + " / " + String(cfg.radio.bandwidth, 1));
    addReadOnlyRow(t("lbl_coding_rate"), String(cfg.radio.codingRate));
    // Scope/region + path-hash size are editable on-device (gated full settings); both reboot
    // on leave to re-apply (scope re-derives _globalScope, hash re-reads at initRadio).
    addNavRowGated(t("lbl_scope"), cfg.radio.scope, scopeRowCb, false);
    {
        char phBuf[16];
        snprintf(phBuf, sizeof(phBuf), "%u B/hop", (unsigned)(cfg.radio.pathHashMode + 1));
        addNavRowGated(t("lbl_path_hash"), phBuf, pathHashRowCb, false);
    }
    addReadOnlyRow(t("lbl_status"),
                   MeshManager::instance().isRadioReady() ? t("ready") : t("error"));
    if (MeshManager::instance().isRadioReady()) {
        float duty = MeshManager::instance().getTxDutyCyclePercent();
        char utilBuf[32];
        if (MeshManager::instance().isEURegion()) {
            snprintf(utilBuf, sizeof(utilBuf), "%.2f%% (10%% limit)", duty);
        } else {
            snprintf(utilBuf, sizeof(utilBuf), "%.2f%%", duty);
        }
        addReadOnlyRow(t("ch_util"), utilBuf);
    }
}

void SettingsScreen::buildDisplay() {
    const auto& cfg = ConfigManager::instance().config();

    // Brightness family + theme are the "basic" controls — editable in Restricted.
    addNavRowGated(t("lbl_theme"), themeDisplayName(cfg.display.theme), themeRowCb, true);
    addSliderRowGated(t("lbl_brightness"), &_brightnessSlider, &_brightnessValLbl,
                      10, 255, cfg.display.brightness, String(cfg.display.brightness), true);
    addSliderRowGated(t("lbl_auto_dim"), &_autoDimSlider, &_autoDimValLbl,
                      0, 300, cfg.display.autoDimSeconds,
                      cfg.display.autoDimSeconds > 0 ? (String(cfg.display.autoDimSeconds) + "s") : String(t("off")), true);
    addSliderRowGated(t("lbl_dim_brightness"), &_dimBrightnessSlider, &_dimBrightnessValLbl,
                      0, 255, cfg.display.dimBrightness,
                      cfg.display.dimBrightness > 0 ? String(cfg.display.dimBrightness) : String(t("off")), true);
    addSliderRowGated(t("lbl_kbd_backlight"), &_kbdBrightnessSlider, &_kbdBrightnessValLbl,
                      1, 255, cfg.display.kbdBrightness, String(cfg.display.kbdBrightness), true);
    addSwitchRowGated(t("lbl_emoji"), cfg.display.emoji, emojiToggleCb, nullptr, false);
    // Colour glyphs for the on-device emoji set; everything else stays monochrome.
    addSwitchRowGated(t("lbl_color_emoji"), cfg.display.colorEmoji, boolToggleCb,
                      (void*)BoolField::ColorEmoji, false);
#ifndef PLATFORM_TWATCH
    // T-Deck only — T-Watch reaches Admin via the PEK short-press and has no
    // room for the gear in its two-row status bar.
    addSwitchRowGated(t("lbl_menu_button"), cfg.display.menuButton, menuButtonToggleCb, nullptr, false);
#endif

    // Screenshots and debug overlays fold into Display (no separate Debug screen / header).
    addSwitchRowGated(t("lbl_screenshots"),  cfg.debug.screenshots, screenshotsToggleCb,  nullptr, false);
    addSwitchRowGated(t("lbl_show_memory"),  cfg.debug.showMemory,  showMemoryToggleCb,   nullptr, false);
}

void SettingsScreen::buildMessaging() {
    const auto& cfg = ConfigManager::instance().config();

    addSwitchRowGated(t("lbl_history"), cfg.messaging.saveHistory, boolToggleCb,
                      (void*)BoolField::SaveHistory, false);
    addSliderRowGated(t("lbl_max_per_chat"), &_maxHistorySlider, &_maxHistoryValLbl,
                      0, 500, cfg.messaging.maxHistoryPerChat, String(cfg.messaging.maxHistoryPerChat), false);

    String lf = cfg.messaging.locationFormat;
    String lfLabel = (lf == "mgrs") ? t("loc_mgrs") : (lf == "both") ? t("loc_both") : t("loc_decimal");
    addNavRowGated(t("lbl_location_format"), lfLabel, locFormatRowCb, false);

    addSliderRowGated(t("lbl_max_retries"), &_maxRetriesSlider, &_maxRetriesValLbl,
                      1, 5, cfg.messaging.maxRetries, String(cfg.messaging.maxRetries), false);
    addSwitchRowGated(t("lbl_req_telemetry"), cfg.messaging.requestTelemetry, boolToggleCb,
                      (void*)BoolField::RequestTelemetry, false);

    String st = cfg.messaging.showTelemetry;
    String stLabel = (st == "battery") ? t("tel_battery")
                   : (st == "location") ? t("tel_location")
                   : (st == "both") ? t("tel_both") : t("off");
    addNavRowGated(t("lbl_telemetry_badges"), stLabel, showTelemetryRowCb, false);

    addSwitchRowGated(t("lbl_auto_telemetry"), cfg.messaging.autoTelemetry, boolToggleCb,
                      (void*)BoolField::AutoTelemetry, false);
    addSwitchRowGated(t("lbl_canned"), cfg.messaging.cannedMessages, boolToggleCb,
                      (void*)BoolField::CannedMessages, false);
    if (cfg.messaging.cannedMessages) {
        int cnt = cfg.messaging.cannedCustom.empty() ? 8 : (int)cfg.messaging.cannedCustom.size();
        addNavRow(t("sec_canned_messages_t"), String(cnt), cannedSectionRowCb);
    }
    addSwitchRowGated(t("lbl_allow_mute"), cfg.messaging.allowMute, boolToggleCb,
                      (void*)BoolField::AllowMute, false);
    addSwitchRowGated(t("lbl_show_hops"), cfg.messaging.showHopCount, boolToggleCb,
                      (void*)BoolField::ShowHopCount, false);
    // Controls the share (↑) button in a DM chat header — UIManager reads this
    // when deciding which header buttons to show.
    addSwitchRowGated(t("lbl_share_contact"), cfg.messaging.shareContact, boolToggleCb,
                      (void*)BoolField::ShareContact, false);
    // Emoji reactions. Only MeshCore One and MCLite understand these; on other
    // clients a reaction arrives as a plain text message (emoji + 8-char hash).
    addSwitchRowGated(t("lbl_reactions"), cfg.messaging.reactions, boolToggleCb,
                      (void*)BoolField::Reactions, false);
}

void SettingsScreen::buildSound() {
    const auto& cfg = ConfigManager::instance().config();

    addSwitchRowGated(t("lbl_sound"), cfg.soundEnabled, soundToggleCb, nullptr, false);
    addNavRowGated(t("lbl_sos_keyword"), cfg.sosKeyword, sosKeywordRowCb, false);
    addSliderRowGated(t("lbl_sos_repeat"), &_sosRepeatSlider, &_sosRepeatValLbl,
                      1, 10, cfg.sosRepeat, String(cfg.sosRepeat), false);
    if (_sosRepeatSlider && !cfg.soundEnabled) {
        lv_obj_add_state(_sosRepeatSlider, LV_STATE_DISABLED);
        lv_obj_add_state(_sosRepeatValLbl, LV_STATE_DISABLED);
    }
}

void SettingsScreen::buildGps() {
    const auto& cfg = ConfigManager::instance().config();

    addSwitchRowGated(t("lbl_gps"), cfg.gpsEnabled, gpsToggleCb, nullptr, false);

    if (cfg.gpsEnabled) {
        addNavRowGated(t("lbl_location_advert"), locPrecisionLabel(cfg.locationPrecision), locPrecisionRowCb, false);

        // Compact display: the POSIX TZ string is long, so show just the leading
        // abbreviation (chars before the first digit/sign) + auto-DST hint. The
        // editor shows/edits the full string.
        String tzVal;
        if (cfg.gpsTimezone.length() > 0) {
            for (size_t i = 0; i < cfg.gpsTimezone.length(); i++) {
                char c = cfg.gpsTimezone[i];
                if (c == '-' || c == '+' || (c >= '0' && c <= '9')) break;
                tzVal += c;
            }
            tzVal += " (auto-DST)";
        } else {
            tzVal = t("off");
        }
        addNavRowGated(t("lbl_timezone"), tzVal, timezoneRowCb, false);

        addSliderRowGated(t("lbl_clock_offset"), &_clockOffsetSlider, &_clockOffsetValLbl,
                          -12, 14, cfg.gpsClockOffset, String(cfg.gpsClockOffset) + "h", false);

        // Last-known max age in minutes (60s..7200s → 1..120 min).
        addSliderRowGated(t("lbl_last_known_max"), &_lastKnownSlider, &_lastKnownValLbl,
                          1, 120, cfg.gpsLastKnownMaxAge / 60, String(cfg.gpsLastKnownMaxAge / 60) + "m", false);

        // Live read-only diagnostics.
        auto& gps = GPS::instance();
        FixStatus fs = gps.fixStatus();
        switch (fs) {
            case FixStatus::LIVE: {
                addReadOnlyRow(t("gps_fix_status"), t("gps_live"));
                addReadOnlyRow(t("gps_coords"), gps.formatLocation());
                addReadOnlyRow(t("gps_satellites"), String(gps.satellites()));
                char hdopBuf[8];
                snprintf(hdopBuf, sizeof(hdopBuf), "%.1f", gps.hdop());
                addReadOnlyRow(t("lbl_hdop"), String(hdopBuf));
                break;
            }
            case FixStatus::LAST_KNOWN: {
                uint32_t age = gps.fixAgeSeconds();
                char ageBuf[32];
                if (age < 60)        snprintf(ageBuf, sizeof(ageBuf), t("gps_last_known_s"), (int)age);
                else if (age < 3600) snprintf(ageBuf, sizeof(ageBuf), t("gps_last_known_m"), (int)(age / 60));
                else                 snprintf(ageBuf, sizeof(ageBuf), t("gps_last_known_h"), (int)(age / 3600));
                addReadOnlyRow(t("gps_fix_status"), String(ageBuf));
                addReadOnlyRow(t("gps_coords"), gps.formatLocation());
                break;
            }
            case FixStatus::NO_FIX:
                addReadOnlyRow(t("gps_fix_status"), t("searching"));
                break;
        }
    }
}

void SettingsScreen::buildBattery() {
    const auto& cfg = ConfigManager::instance().config();

    addSwitchRowGated(t("lbl_low_alert"), cfg.battery.lowAlertEnabled, lowAlertToggleCb, nullptr, false);
    addSliderRowGated(t("lbl_low_alert_threshold"), &_lowAlertSlider, &_lowAlertValLbl,
                      5, 50, cfg.battery.lowAlertThreshold, String(cfg.battery.lowAlertThreshold) + "%", false);
    if (_lowAlertSlider && !cfg.battery.lowAlertEnabled) {
        lv_obj_add_state(_lowAlertSlider, LV_STATE_DISABLED);
        lv_obj_add_state(_lowAlertValLbl, LV_STATE_DISABLED);
    }

    // Read-only diagnostics.
    addReadOnlyRow(t("lbl_level"), String(Battery::instance().percent()) + "%");

    {
        uint32_t bootEp = TimeHelper::instance().bootEpoch();
        char tsBuf[20], agoBuf[16];
        TimeHelper::instance().formatTimestamp(bootEp, tsBuf, sizeof(tsBuf));
        uint32_t nowEp = TimeHelper::instance().bestEpoch();
        if (bootEp >= 1700000000 && nowEp > bootEp) {
            TimeHelper::formatAgo(nowEp - bootEp, agoBuf, sizeof(agoBuf));
        } else {
            strncpy(agoBuf, "--", sizeof(agoBuf));
            agoBuf[sizeof(agoBuf) - 1] = '\0';
        }
        char rowBuf[48];
        snprintf(rowBuf, sizeof(rowBuf), "%s (%s)", tsBuf, agoBuf);
        addReadOnlyRow(t("lbl_uptime"), rowBuf);
    }

    {
        auto& batt = Battery::instance();
        if (!batt.isCharging()) {
            uint32_t lcEp = batt.lastChargedEpoch();
            if (lcEp >= 1700000000) {
                char tsBuf[20], agoBuf[16];
                TimeHelper::instance().formatTimestamp(lcEp, tsBuf, sizeof(tsBuf));
                uint32_t nowEp = TimeHelper::instance().bestEpoch();
                if (nowEp >= lcEp) {
                    TimeHelper::formatAgo(nowEp - lcEp, agoBuf, sizeof(agoBuf));
                } else {
                    strncpy(agoBuf, "--", sizeof(agoBuf));
                    agoBuf[sizeof(agoBuf) - 1] = '\0';
                }
                char rowBuf[48];
                snprintf(rowBuf, sizeof(rowBuf), "%s (%s, %d%%)",
                         tsBuf, agoBuf, (int)batt.lastChargedPercent());
                addReadOnlyRow(t("lbl_last_charged"), rowBuf);
            }
        }
    }
}

void SettingsScreen::buildSecurity() {
    const auto& cfg = ConfigManager::instance().config();

    String lockModeValue = t("off");
    if (cfg.security.lockMode == "key") lockModeValue = t("lock_key");
    else if (cfg.security.lockMode == "pin") lockModeValue = t("lock_pin");
    addNavRowGated(t("lbl_lock_mode"), lockModeValue, lockModeRowCb, false);

    addNavRowGated(t("lbl_pin_code"),
                   cfg.security.pinCode.length() > 0 ? String("****") : String(t("off")), pinRowCb, false);

    String autoLockValue = t("off");
    if (cfg.security.autoLock == "key") autoLockValue = t("lock_key");
    else if (cfg.security.autoLock == "pin") autoLockValue = t("lock_pin");
    addNavRowGated(t("lbl_auto_lock"), autoLockValue, autoLockRowCb, false);

    // Admin lockout. Locking hides the whole Admin hub; the way back in is the '0'
    // key (T-Deck) or the side button (T-Watch), which then asks for the admin PIN.
    addNavRowGated(t("lbl_admin_pin"),
                   cfg.security.adminPin.length() > 0 ? String("****") : String(t("off")),
                   adminPinRowCb, false);
    addNavRowGated(t("lbl_admin_lock"), t("admin_lock_action"), adminLockRowCb, false);

    // Permissions. basic=false is load-bearing: it means these rows are only
    // editable while settings=="full", so the device can tighten but never
    // loosen itself. Recovery from a lockdown is editing config.json on the SD.
    const String& ps = cfg.permissions.settings;
    String psLabel = (ps == "none") ? t("perm_none")
                   : (ps == "restricted") ? t("perm_restricted") : t("perm_full");
    addNavRowGated(t("lbl_perm_settings"), psLabel, permSettingsRowCb, false);
    addSwitchRowGated(t("lbl_perm_convmgmt"), cfg.permissions.conversationManagement,
                      boolToggleCb, (void*)BoolField::PermConvMgmt, false);
    addSwitchRowGated(t("lbl_perm_companion"), cfg.permissions.companion,
                      boolToggleCb, (void*)BoolField::PermCompanion, false);
}

// ─────────────────────────── Canned messages section ───────────────────────────

void SettingsScreen::cannedSectionRowCb(lv_event_t* e) {
    auto* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openCannedList(CannedTarget::Global, -1);
}

// Resolve the target to the actual vector being edited. Returns nullptr if the
// target index no longer exists (e.g. the contact was deleted while the editor
// was open), so every caller must null-check.
std::vector<String>* SettingsScreen::cannedList() const {
    auto& c = ConfigManager::instance().config();
    switch (_cannedTarget) {
        case CannedTarget::Global:  return &c.messaging.cannedCustom;
        case CannedTarget::Contact:
            if (_cannedTargetIdx >= 0 && (size_t)_cannedTargetIdx < c.contacts.size())
                return &c.contacts[_cannedTargetIdx].canned;
            return nullptr;
        case CannedTarget::Channel:
            if (_cannedTargetIdx >= 0 && (size_t)_cannedTargetIdx < c.channels.size())
                return &c.channels[_cannedTargetIdx].canned;
            return nullptr;
        case CannedTarget::Room:
            if (_cannedTargetIdx >= 0 && (size_t)_cannedTargetIdx < c.roomServers.size())
                return &c.roomServers[_cannedTargetIdx].canned;
            return nullptr;
    }
    return nullptr;
}

// The chat picker reads a DM's quick replies from ContactStore, not from config
// (ChatScreen.cpp, ConvoId::DM branch), so editing config alone leaves the running
// session showing the old list until the next boot. Channels and rooms are read
// straight from config and need no sync. Match on the public key rather than the
// index: ContactStore::loadFromConfig() skips contacts whose key fails to parse,
// so its indices can diverge from config's.
void SettingsScreen::syncCannedToRuntime() const {
    if (_cannedTarget != CannedTarget::Contact) return;
    auto& c = ConfigManager::instance().config();
    if (_cannedTargetIdx < 0 || (size_t)_cannedTargetIdx >= c.contacts.size()) return;
    const String& b64 = c.contacts[_cannedTargetIdx].publicKey;
    auto& cs = ContactStore::instance();
    for (size_t i = 0; i < cs.count(); i++) {
        Contact* sc = cs.findByIndex(i);
        if (sc && sc->publicKeyB64.equalsIgnoreCase(b64)) {
            sc->canned = c.contacts[_cannedTargetIdx].canned;
            return;
        }
    }
}

// Name of the conversation being edited, for the section header. Empty = global.
String SettingsScreen::cannedTargetName() const {
    auto& c = ConfigManager::instance().config();
    switch (_cannedTarget) {
        case CannedTarget::Contact:
            if (_cannedTargetIdx >= 0 && (size_t)_cannedTargetIdx < c.contacts.size())
                return c.contacts[_cannedTargetIdx].alias;
            break;
        case CannedTarget::Channel:
            if (_cannedTargetIdx >= 0 && (size_t)_cannedTargetIdx < c.channels.size())
                return c.channels[_cannedTargetIdx].name;
            break;
        case CannedTarget::Room:
            if (_cannedTargetIdx >= 0 && (size_t)_cannedTargetIdx < c.roomServers.size())
                return c.roomServers[_cannedTargetIdx].name;
            break;
        default: break;
    }
    return String();
}

void SettingsScreen::openCannedList(CannedTarget t, int idx) {
    _cannedTarget    = t;
    _cannedTargetIdx = idx;
    UIManager::instance().showSettings(SettingsSection::CannedMessages);
}

void SettingsScreen::buildCannedList() {
    const bool edit = canEdit(false);
    const auto& cfg = ConfigManager::instance().config();

    const std::vector<String>* list = cannedList();
    const bool isGlobal = (_cannedTarget == CannedTarget::Global);

    // Effective list. Global: an empty list means "use the built-in defaults", so
    // show them. Per-conversation: an empty list means "fall back to the global
    // list", so show nothing — seeding it with defaults here would silently turn
    // a follows-global conversation into an override.
    std::vector<String> messages;
    if (list && !list->empty()) {
        messages = *list;
    } else if (isGlobal) {
        for (int i = 1; i <= 8; i++) {
            char key[12];
            snprintf(key, sizeof(key), "canned_%d", i);
            messages.push_back(t(key));
        }
    }

    char hdr[64];
    String who = cannedTargetName();
    if (who.length() > 0) snprintf(hdr, sizeof(hdr), "%s (%d)", who.c_str(), (int)messages.size());
    else                  snprintf(hdr, sizeof(hdr), t("sec_canned_messages"), (int)messages.size());
    addSectionHeader(hdr);

    // Per-conversation lists that are empty follow the global list; say so rather
    // than showing a bare, unexplained empty screen.
    if (!isGlobal && messages.empty()) addReadOnlyRow(t("canned_uses_global"), "");

    if (edit && (int)messages.size() < 8)
        addNavRow((String(LV_SYMBOL_PLUS " ") + t("canned_add_msg")).c_str(), "", cannedAddRowCb);

    for (size_t i = 0; i < messages.size(); i++) {
        if (edit) {
            lv_obj_t* row = addNavRow(("  " + String((int)(i + 1))).c_str(), messages[i], cannedRowCb);
            lv_obj_set_user_data(row, (void*)(intptr_t)i);
        } else {
            addReadOnlyRow(("  " + String((int)(i + 1))).c_str(), messages[i]);
        }
    }
}

void SettingsScreen::cannedAddRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    self->openCannedEditor(-1, "");
}

void SettingsScreen::cannedRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    const std::vector<String>* list = self->cannedList();
    String text;
    if (list && (size_t)idx < list->size()) {
        text = (*list)[idx];
    } else {
        char key[12];
        snprintf(key, sizeof(key), "canned_%d", idx + 1);
        text = t(key);
    }
    self->openCannedEditor(idx, text);
}

void SettingsScreen::openCannedEditor(int idx, const String& text) {
    if (_cannedTextarea) return;
    _cannedEditIdx = idx;

    _cannedOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_cannedOverlay, Display::width(), Display::height());
    lv_obj_set_pos(_cannedOverlay, 0, 0);
    lv_obj_set_style_bg_color(_cannedOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(_cannedOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_cannedOverlay, 0, 0);
    lv_obj_clear_flag(_cannedOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLbl = lv_label_create(_cannedOverlay);
    lv_obj_set_style_text_font(titleLbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(titleLbl, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(titleLbl, t("canned_enter_msg"));
    lv_obj_align(titleLbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    _cannedTextarea = lv_textarea_create(_cannedOverlay);
    lv_textarea_set_one_line(_cannedTextarea, true);
    lv_obj_set_width(_cannedTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(_cannedTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(_cannedTextarea, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_textarea_set_max_length(_cannedTextarea, 80);
    lv_textarea_set_text(_cannedTextarea, text.c_str());
    lv_textarea_set_placeholder_text(_cannedTextarea, t("canned_enter_msg"));

    lv_obj_t* btnRow = lv_obj_create(_cannedOverlay);
    lv_obj_set_size(btnRow, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btnRow, theme::PAD_SMALL, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_width(save, LV_PCT(100));
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, cannedSaveCb, LV_EVENT_CLICKED, this);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    _cannedDelBtn = lv_btn_create(btnRow);
    lv_obj_set_width(_cannedDelBtn, LV_PCT(100));
    lv_obj_set_style_bg_color(_cannedDelBtn, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(_cannedDelBtn, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(_cannedDelBtn, cannedDeleteCb, LV_EVENT_CLICKED, this);
    lv_obj_t* delLbl = lv_label_create(_cannedDelBtn);
    lv_label_set_text(delLbl, t("btn_delete"));
    lv_obj_center(delLbl);
    if (idx < 0) lv_obj_add_flag(_cannedDelBtn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_width(cancel, LV_PCT(100));
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((SettingsScreen*)p)->hideCannedEditor(); }, s);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    _editorGroup = g;
    lv_group_add_obj(g, _cannedTextarea);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, _cannedDelBtn);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(_cannedTextarea);
    UIManager::instance().switchToModalGroup(_cannedOverlay);
    IInput::instance().attachToGroup(g);
    lv_obj_add_event_cb(_cannedTextarea, cannedSaveCb, LV_EVENT_READY, this);

#ifdef PLATFORM_TWATCH
    _cannedKbd = lv_keyboard_create(_cannedOverlay);
    lv_keyboard_set_textarea(_cannedKbd, _cannedTextarea);
    lv_keyboard_set_popovers(_cannedKbd, true);
    lv_btnmatrix_set_btn_ctrl_all(_cannedKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(_cannedKbd, [](lv_event_t* ev) {
        auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED)
            lv_btnmatrix_set_btn_ctrl_all(self->_cannedKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        else if (code == LV_EVENT_CANCEL)
            lv_async_call([](void* p) { ((SettingsScreen*)p)->hideCannedEditor(); }, self);
    }, LV_EVENT_ALL, this);
#endif
}

void SettingsScreen::hideCannedEditor() {
    if (!_cannedTextarea) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
#ifdef PLATFORM_TWATCH
    _cannedKbd = nullptr;
#endif
    _cannedTextarea = nullptr;
    _cannedDelBtn   = nullptr;
    lv_obj_del_async(_cannedOverlay);
    _cannedOverlay  = nullptr;
    if (_screen) show();
}

bool SettingsScreen::convoManageEnabled() const {
    return ConfigManager::instance().config().permissions.conversationManagement && canEdit(false);
}

void SettingsScreen::buildConvoList() {
    const bool manage = convoManageEnabled();
    auto& cfg = ConfigManager::instance().config();

    if (_section == SettingsSection::Contacts) {
        if (manage) {
            char hdr[32];
            snprintf(hdr, sizeof(hdr), t("sec_contacts"), (int)cfg.contacts.size());
            addSectionHeader(hdr);
            addNavRow((String(LV_SYMBOL_PLUS " ") + t("convo_add_contact")).c_str(), "", convoAddRowCb);
            for (size_t i = 0; i < cfg.contacts.size(); i++) {
                String info = cfg.contacts[i].alias;
                if (cfg.contacts[i].sendSos) info += " [SOS]";
                lv_obj_t* row = addNavRow(("  " + String((int)(i + 1))).c_str(), info, convoRowCb);
                lv_obj_set_user_data(row, (void*)(intptr_t)i);
            }
        } else {
            auto& contacts = ContactStore::instance();
            char hdr[32];
            snprintf(hdr, sizeof(hdr), t("sec_contacts"), (int)contacts.count());
            addSectionHeader(hdr);
            for (size_t i = 0; i < contacts.count(); i++) {
                const Contact* c = contacts.findByIndex(i);
                if (!c) continue;
                String info = c->name;
                if (c->sendSos) info += " [SOS]";
                if (c->allowTelemetry && c->allowLocation) info += " [GPS]";
                addReadOnlyRow(("  " + String((int)(i + 1))).c_str(), info);
            }
        }
    } else if (_section == SettingsSection::Channels) {
        if (manage) {
            char hdr[32];
            snprintf(hdr, sizeof(hdr), t("sec_channels"), (int)cfg.channels.size());
            addSectionHeader(hdr);
            addNavRow((String(LV_SYMBOL_PLUS " ") + t("convo_add_channel")).c_str(), "", convoAddRowCb);
            for (size_t i = 0; i < cfg.channels.size(); i++) {
                const auto& ch = cfg.channels[i];
                const char* prefix = (ch.type == "private") ? "  *" : "  #";
                String info = ch.name;
                if (ch.readOnly) info += " [read-only]";
                if (ch.sendSos) info += " [SOS]";
                if (ch.scope.length() > 0) info += " [scope:" + ch.scope + "]";
                lv_obj_t* row = addNavRow(prefix, info, convoRowCb);
                lv_obj_set_user_data(row, (void*)(intptr_t)i);
            }
        } else {
            auto& channels = ChannelStore::instance();
            char hdr[32];
            snprintf(hdr, sizeof(hdr), t("sec_channels"), (int)channels.count());
            addSectionHeader(hdr);
            for (const auto& ch : channels.all()) {
                const char* prefix = ch.isPrivate() ? "  *" : "  #";
                String info = ch.name;
                if (ch.readOnly) info += " [read-only]";
                if (ch.sendSos) info += " [SOS]";
                if (ch.scope.length() > 0) info += " [scope:" + ch.scope + "]";
                addReadOnlyRow(prefix, info);
            }
        }
    } else {  // Rooms
        const auto& rooms = cfg.roomServers;
        char hdr[32];
        snprintf(hdr, sizeof(hdr), t("sec_rooms"), (int)rooms.size());
        addSectionHeader(hdr);
        if (manage)
            addNavRow((String(LV_SYMBOL_PLUS " ") + t("convo_add_room")).c_str(), "", convoAddRowCb);
        auto& store = MessageStore::instance();
        for (size_t i = 0; i < rooms.size(); i++) {
            String info = rooms[i].name;
            info += UIManager::instance().isRoomLoggedIn(i) ? " [online]" : " [offline]";
            if (rooms[i].scope.length() > 0) info += " [scope:" + rooms[i].scope + "]";
            if (!manage && rooms[i].publicKey.length() == 64) {
                String shortId = rooms[i].publicKey.substring(0, 16);
                ConvoId rid { ConvoId::ROOM, shortId };
                if (Conversation* convo = store.getConversation(rid)) {
                    if (convo->syncSince > 0) {
                        char ts[24];
                        time_t tt = (time_t)convo->syncSince;
                        struct tm* tm_info = gmtime(&tt);
                        if (tm_info) {
                            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm_info);
                            info += " ";
                            info += ts;
                        }
                    }
                }
            }
            if (manage) {
                lv_obj_t* row = addNavRow("  R", info, convoRowCb);
                lv_obj_set_user_data(row, (void*)(intptr_t)i);
            } else {
                addReadOnlyRow("  R", info);
            }
        }
    }
}

// ─────────────────── Conversation add/remove (gated by permission) ───────────────────

// "Add …" row → a type chooser (contacts/channels) or straight into the editor (rooms).
void SettingsScreen::convoAddRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;

    if (self->_section == SettingsSection::Rooms) {
        self->openConvoEditor(ConvoField::RoomName);
        return;
    }

    self->openButtonModal(ConvoModal::AddChooser);
}

// Tap an existing entry → confirm delete → remove from config (+ history) → reboot prompt.
void SettingsScreen::convoRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    size_t idx = (size_t)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    self->_convPendKey = String((int)idx);  // stash index for the confirm handler

    auto& cfg = ConfigManager::instance().config();
    bool valid = (self->_section == SettingsSection::Contacts && idx < cfg.contacts.size()) ||
                 (self->_section == SettingsSection::Channels && idx < cfg.channels.size()) ||
                 (self->_section == SettingsSection::Rooms    && idx < cfg.roomServers.size());
    if (!valid) return;
    self->openButtonModal(ConvoModal::DeleteConfirm);  // index carried in _convPendKey
}

void SettingsScreen::showConvoRebootConfirm() {
    openButtonModal(ConvoModal::RebootConfirm);
}

// ── Vertical button-list modal (picker styling) + its dispatch ──
namespace {
std::vector<String> g_btnModalLabels;
}  // namespace

void SettingsScreen::openButtonModal(ConvoModal purpose) {
    if (_convoModalBtnm) return;
    _convoModal = purpose;

    // Labels + optional title/body per purpose.
    g_btnModalLabels.clear();
    String title;
    String body;
    switch (purpose) {
        case ConvoModal::AddChooser:
            if (_section == SettingsSection::Contacts) {
                title = t("convo_add_contact");
                g_btnModalLabels = { t("convo_add_from_heard"), t("convo_add_manual"), t("btn_cancel") };
            } else {  // Channels
                title = t("convo_add_channel");
                g_btnModalLabels = { t("chan_type_public"), t("chan_type_hashtag"), t("chan_type_private"), t("btn_cancel") };
            }
            break;
        case ConvoModal::DeleteConfirm: {
            auto& cfg = ConfigManager::instance().config();
            size_t i = (size_t)_convPendKey.toInt();
            String name;
            if (_section == SettingsSection::Contacts && i < cfg.contacts.size())      name = cfg.contacts[i].alias;
            else if (_section == SettingsSection::Channels && i < cfg.channels.size())  name = cfg.channels[i].name;
            else if (_section == SettingsSection::Rooms && i < cfg.roomServers.size())  name = cfg.roomServers[i].name;
            title = name;
            // Row actions: an extra per-section action at idx 0 (Reset Path for contacts; Set
            // Scope for channels and rooms), then Delete (idx 1), then Cancel.
            // Row actions: per-section action at idx 0 (Reset Path for contacts,
            // Set Scope for channels/rooms), Quick Replies at idx 1, Delete at
            // idx 2, Cancel last. deleteIdx below MUST track this ordering.
            if (_section == SettingsSection::Contacts)
                g_btnModalLabels = { t("convo_reset_path"), t("sec_canned_messages_t"), t("btn_delete"), t("btn_cancel") };
            else
                g_btnModalLabels = { t("convo_set_scope"), t("sec_canned_messages_t"), t("btn_delete"), t("btn_cancel") };
            break;
        }
        case ConvoModal::RebootConfirm:
            title = t("heard_saved_msg");
            g_btnModalLabels = { t("reboot_now"), t("btn_ok") };
            break;
        case ConvoModal::OffgridConfirm: {
            const auto& cfg = ConfigManager::instance().config();
            bool enabling = !cfg.offgrid.enabled;
            char buf[192];
            if (enabling) snprintf(buf, sizeof(buf), t("offgrid_confirm_on_body"),
                                   (int)mclite::offgridFreqFor(cfg.radio.frequency));
            else          snprintf(buf, sizeof(buf), t("offgrid_confirm_off_body"), cfg.radio.frequency);
            title = buf;
            g_btnModalLabels = { t("reboot_now"), t("btn_cancel") };
            break;
        }
        case ConvoModal::PermLockConfirm: {
            title = t("perm_lock_title");
            // Name what is actually lost. companion is UI-visibility only, so it
            // must not claim to stop anything already running.
            switch (_permPending) {
                case PermField::Settings:  body = t("perm_lock_settings_body");  break;
                case PermField::ConvMgmt:  body = t("perm_lock_convmgmt_body");  break;
                case PermField::Companion: body = t("perm_lock_companion_body"); break;
                default: return;
            }
            g_btnModalLabels = { t("perm_lock_confirm"), t("btn_cancel") };
            break;
        }
        case ConvoModal::AdminLockConfirm:
            // Explain before asking for the PIN. Locking Admin removes every
            // on-device settings screen, so the consequence and the way back have
            // to be stated plainly -- a bare PIN pad tells the user nothing.
            title = t("admin_lock_title");
            body  = t("admin_lock_body");
            g_btnModalLabels = { t("admin_lock_action"), t("btn_cancel") };
            break;
        default: return;
    }

    _convoModalBtnm = ModalDialog::show(title, body, g_btnModalLabels,
        [this](lv_obj_t* dlg, int idx) { onConvoModalChoice(dlg, idx); });
}

// Commit the confirmed tightening. Saves straight away rather than relying on
// g_dsDirty: once settings goes to restricted/none the rows are read-only, and a
// value the user explicitly confirmed should not depend on how they leave the
// screen.
void SettingsScreen::applyPendingPermission() {
    auto& mgr = ConfigManager::instance();
    auto& c   = mgr.config();
    switch (_permPending) {
        case PermField::Settings:
            if (_permPendingValue.length()) c.permissions.settings = _permPendingValue;
            break;
        case PermField::ConvMgmt:  c.permissions.conversationManagement = false; break;
        case PermField::Companion: c.permissions.companion             = false; break;
        default: return;
    }
    _permPending = PermField::None;
    _permPendingValue = "";
    mgr.save();
}

void SettingsScreen::hideButtonModal() {
    if (!_convoModalBtnm) return;
    ModalDialog::close(_convoModalBtnm);
    _convoModalBtnm = nullptr;
    _convoModal = ConvoModal::None;
}

void SettingsScreen::onConvoModalChoice(lv_obj_t* dlg, int idxIn) {
    SettingsScreen* self = this;
    size_t idx = (size_t)idxIn;
    ConvoModal purpose = _convoModal;
    bool isContacts = (_section == SettingsSection::Contacts);
    ModalDialog::close(dlg);
    _convoModalBtnm = nullptr;
    _convoModal = ConvoModal::None;

    if (purpose == ConvoModal::AddChooser) {
        if (isContacts) {
            if (idx == 0)      UIManager::instance().showScreen(Screen::HEARD_ADVERTS);
            else if (idx == 1) lv_async_call([](void* p){ ((SettingsScreen*)p)->openConvoEditor(ConvoField::ContactAlias); }, self);
            else               lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self);
        } else {  // channels
            if (idx == 0) {  // Public — one tap
                if (ConfigManager::instance().hasPublicChannel()) {
                    UIManager::instance().showToast(t("chan_public_exists"));
                    lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self);
                } else {
                    ChannelConfig cc; cc.type = "public";
                    if (ConfigManager::instance().appendChannel(cc))
                        lv_async_call([](void* p){ ((SettingsScreen*)p)->showConvoRebootConfirm(); }, self);
                    else { UIManager::instance().showToast(t("err_save_failed"));
                           lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self); }
                }
            } else if (idx == 1) lv_async_call([](void* p){ ((SettingsScreen*)p)->openConvoEditor(ConvoField::ChanHashName); }, self);
            else if (idx == 2)   lv_async_call([](void* p){ ((SettingsScreen*)p)->openConvoEditor(ConvoField::ChanPrivName); }, self);
            else                 lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self);
        }
        return;
    }

    if (purpose == ConvoModal::DeleteConfirm) {
        size_t i = (size_t)self->_convPendKey.toInt();
        auto& c = ConfigManager::instance().config();
        const bool isContact = self->_section == SettingsSection::Contacts;
        const bool isChannel = self->_section == SettingsSection::Channels;
        const bool isRoom    = self->_section == SettingsSection::Rooms;

        // idx 0 is the per-section action (Reset Path for contacts; Set Scope for channels/rooms),
        // then Delete, then Cancel — so Delete is always idx 1.
        if (isContact && idx == 0) {                    // Reset Path
            if (i < c.contacts.size()) {
                String b64 = c.contacts[i].publicKey;
                for (const auto& sc : ContactStore::instance().all())
                    if (sc.publicKeyB64.equalsIgnoreCase(b64)) {
                        if (MeshManager::instance().resetPathByKey(sc.publicKey))
                            UIManager::instance().showToast(t("convo_reset_done"));
                        break;
                    }
            }
            lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self);
            return;
        }
        if ((isChannel || isRoom) && idx == 0) {        // Set Scope (per-channel/room editor)
            self->_scopeTarget = isChannel ? ScopeTarget::Channel : ScopeTarget::Room;
            self->_scopeTargetIdx = (int)i;
            lv_async_call([](void* p){ ((SettingsScreen*)p)->openScopeEditor(); }, self);
            return;
        }

        if (idx == 1) {                                 // Quick Replies (per-conversation)
            CannedTarget t = isContact ? CannedTarget::Contact
                           : isChannel ? CannedTarget::Channel
                                       : CannedTarget::Room;
            self->_cannedTarget    = t;
            self->_cannedTargetIdx = (int)i;
            lv_async_call([](void* p){
                auto* sc = (SettingsScreen*)p;
                sc->openCannedList(sc->_cannedTarget, sc->_cannedTargetIdx);
            }, self);
            return;
        }

        // Delete moved from idx 1 to idx 2 when Quick Replies was inserted above.
        // Getting this wrong makes the Quick Replies button delete the row.
        const size_t deleteIdx = 2;
        if (idx != deleteIdx) { lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self); return; }  // Cancel
        bool ok = false;
        if (self->_section == SettingsSection::Contacts && i < c.contacts.size()) {
            String b64 = c.contacts[i].publicKey;
            ok = ConfigManager::instance().removeContactAt(i);
            if (ok) for (const auto& sc : ContactStore::instance().all())
                if (sc.publicKeyB64.equalsIgnoreCase(b64)) {
                    MessageStore::instance().removeConversation(ConvoId{ConvoId::DM, sc.shortId()});
                    MeshManager::instance().deleteAdvertBlob(sc.publicKey); break; }
        } else if (self->_section == SettingsSection::Channels && i < c.channels.size()) {
            String chName = c.channels[i].name;
            ok = ConfigManager::instance().removeChannelAt(i);
            if (ok) MessageStore::instance().removeConversation(ConvoId{ConvoId::CHANNEL, chName});
        } else if (self->_section == SettingsSection::Rooms && i < c.roomServers.size()) {
            String shortId = c.roomServers[i].publicKey.substring(0, 16);
            ok = ConfigManager::instance().removeRoomAt(i);
            if (ok) MessageStore::instance().removeConversation(ConvoId{ConvoId::ROOM, shortId});
        }
        if (ok) lv_async_call([](void* p){ ((SettingsScreen*)p)->showConvoRebootConfirm(); }, self);
        else { UIManager::instance().showToast(t("err_save_failed"));
               lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self); }
        return;
    }

    if (purpose == ConvoModal::RebootConfirm) {
        if (idx == 0) { delay(200); ESP.restart(); }
        else lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self);
        return;
    }

    if (purpose == ConvoModal::AdminLockConfirm) {
        if (idx == 0) {
            // Explained and accepted; typing the PIN is what actually arms it.
            lv_async_call([](void*){
                UIManager::instance().showPinLock(UIManager::PinPurpose::ConfirmAdminLock);
            }, nullptr);
        } else {
            lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self);
        }
        return;
    }
    if (purpose == ConvoModal::PermLockConfirm) {
        if (idx == 0) self->applyPendingPermission();
        else          self->_permPending = PermField::None;   // Cancel: discard
        // show() rebuilds from config either way, so a cancelled switch snaps back.
        lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self);
        return;
    }
    if (purpose == ConvoModal::OffgridConfirm) {
        if (idx == 0) {  // Reboot now — flip offgrid, persist, restart
            auto& mgr = ConfigManager::instance();
            mgr.config().offgrid.enabled = !mgr.config().offgrid.enabled;
            mgr.save();
            delay(200); ESP.restart();
        } else lv_async_call([](void* p){ ((SettingsScreen*)p)->show(); }, self);
        return;
    }
}

void SettingsScreen::pskGenerateCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_convoTextarea) return;
    char hex[33];
    for (int w = 0; w < 4; w++) snprintf(hex + w * 8, 9, "%08x", (unsigned)esp_random());
    hex[32] = '\0';
    lv_textarea_set_text(self->_convoTextarea, hex);
}

void SettingsScreen::setConvoField(ConvoField f) {
    _convoField = f;
    const char* title = "";
    uint32_t maxLen = 32;
    bool gen = false;
    switch (f) {
        case ConvoField::ContactAlias: title = t("convo_enter_alias");     maxLen = 20; break;
        case ConvoField::ContactKey:   title = t("convo_enter_pubkey");    maxLen = 64; break;
        case ConvoField::ChanHashName: title = t("chan_enter_name");       maxLen = 24; break;
        case ConvoField::ChanPrivName: title = t("chan_enter_name");       maxLen = 24; break;
        case ConvoField::ChanPrivPsk:  title = t("chan_enter_psk");        maxLen = 64; gen = true; break;
        case ConvoField::RoomName:     title = t("convo_enter_room_name"); maxLen = 20; break;
        case ConvoField::RoomKey:      title = t("convo_enter_pubkey");    maxLen = 64; break;
        case ConvoField::RoomPass:     title = t("convo_enter_room_pass"); maxLen = 15; break;
    }
    if (_convoTitleLbl) lv_label_set_text(_convoTitleLbl, title);
    if (_convoTextarea) {
        lv_textarea_set_text(_convoTextarea, "");
        lv_textarea_set_max_length(_convoTextarea, maxLen);
        lv_textarea_set_placeholder_text(_convoTextarea, title);
    }
    if (_convoGenBtn) {
        if (gen) lv_obj_clear_flag(_convoGenBtn, LV_OBJ_FLAG_HIDDEN);
        else     lv_obj_add_flag(_convoGenBtn, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsScreen::openConvoEditor(ConvoField f) {
    if (_convoTextarea) return;
    _convPendName = "";
    _convPendKey  = "";

    _convoOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_convoOverlay, Display::width(), Display::height());
    lv_obj_set_pos(_convoOverlay, 0, 0);
    lv_obj_set_style_bg_color(_convoOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(_convoOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_convoOverlay, 0, 0);
    lv_obj_clear_flag(_convoOverlay, LV_OBJ_FLAG_SCROLLABLE);

    _convoTitleLbl = lv_label_create(_convoOverlay);
    lv_obj_set_style_text_font(_convoTitleLbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(_convoTitleLbl, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(_convoTitleLbl, "");
    lv_obj_align(_convoTitleLbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    _convoTextarea = lv_textarea_create(_convoOverlay);
    lv_textarea_set_one_line(_convoTextarea, true);
    lv_obj_set_width(_convoTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(_convoTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(_convoTextarea, theme::ACCENT(), LV_STATE_FOCUSED);

    // Buttons stacked full-width (one per row) so labels stay readable on the
    // narrow screens — Save / Generate (PSK only) / Cancel.
    lv_obj_t* btnRow = lv_obj_create(_convoOverlay);
    lv_obj_set_size(btnRow, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btnRow, theme::PAD_SMALL, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_width(save, LV_PCT(100));
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, convoEditorReadyCb, LV_EVENT_CLICKED, this);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    _convoGenBtn = lv_btn_create(btnRow);
    lv_obj_set_width(_convoGenBtn, LV_PCT(100));
    lv_obj_set_style_bg_color(_convoGenBtn, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(_convoGenBtn, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(_convoGenBtn, pskGenerateCb, LV_EVENT_CLICKED, this);
    lv_obj_t* genLbl = lv_label_create(_convoGenBtn);
    lv_label_set_text(genLbl, t("chan_psk_generate"));
    lv_obj_center(genLbl);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_width(cancel, LV_PCT(100));
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((SettingsScreen*)p)->hideConvoEditor(); }, s);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    _editorGroup = g;
    lv_group_add_obj(g, _convoTextarea);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, _convoGenBtn);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(_convoTextarea);
    UIManager::instance().switchToModalGroup(_convoOverlay);
    IInput::instance().attachToGroup(g);
    lv_obj_add_event_cb(_convoTextarea, convoEditorReadyCb, LV_EVENT_READY, this);

#ifdef PLATFORM_TWATCH
    _convoKbd = lv_keyboard_create(_convoOverlay);
    lv_keyboard_set_textarea(_convoKbd, _convoTextarea);
    lv_keyboard_set_popovers(_convoKbd, true);
    lv_btnmatrix_set_btn_ctrl_all(_convoKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(_convoKbd, [](lv_event_t* ev) {
        auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_convoKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((SettingsScreen*)p)->hideConvoEditor(); }, self);
        }
    }, LV_EVENT_ALL, this);
#endif

    setConvoField(f);
}

void SettingsScreen::hideConvoEditor() {
    if (!_convoTextarea) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
#ifdef PLATFORM_TWATCH
    _convoKbd = nullptr;
#endif
    _convoTextarea = nullptr;
    _convoTitleLbl = nullptr;
    _convoGenBtn   = nullptr;
    lv_obj_del_async(_convoOverlay);
    _convoOverlay = nullptr;
    if (_screen) show();
}

// Trim + lowercase helpers local to the convo editor.
namespace {
String convoTrim(const String& in) {
    const char* s = in.c_str();
    int len = (int)strlen(s), l = 0, r = len - 1;
    while (l <= r && isspace((unsigned char)s[l])) ++l;
    while (r >= l && isspace((unsigned char)s[r])) --r;
    return (l > 0 || r < len - 1) ? in.substring(l, r + 1) : in;
}
bool isHexN(const String& s, int n) {
    if ((int)s.length() != n) return false;
    for (int i = 0; i < n; i++) { char c = s[i];
        if (!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) return false; }
    return true;
}
}  // namespace

void SettingsScreen::cannedSaveCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_cannedTextarea) return;
    String text = convoTrim(String(lv_textarea_get_text(self->_cannedTextarea)));
    if (text.length() == 0) { UIManager::instance().showToast(t("canned_msg_empty")); return; }

    std::vector<String>* list = self->cannedList();
    if (!list) { lv_async_call([](void* p) { ((SettingsScreen*)p)->hideCannedEditor(); }, self); return; }
    int idx = self->_cannedEditIdx;

    // Global only: editing one of the shown defaults materialises all 8 first, so
    // the other seven are preserved rather than lost. A per-conversation list has
    // no defaults to preserve and must stay exactly what the user typed.
    if (self->_cannedTarget == CannedTarget::Global && list->empty()) {
        for (int i = 1; i <= 8; i++) {
            char key[12]; snprintf(key, sizeof(key), "canned_%d", i);
            list->push_back(t(key));
        }
    }
    if (idx < 0) {
        if (list->size() < 8) list->push_back(text);
    } else if ((size_t)idx < list->size()) {
        (*list)[(size_t)idx] = text;
    }
    self->syncCannedToRuntime();
    g_dsDirty = true;
    lv_async_call([](void* p) { ((SettingsScreen*)p)->hideCannedEditor(); }, self);
}

void SettingsScreen::cannedDeleteCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_cannedTextarea) return;
    int idx = self->_cannedEditIdx;
    if (idx < 0) return;

    std::vector<String>* list = self->cannedList();
    if (!list) { lv_async_call([](void* p) { ((SettingsScreen*)p)->hideCannedEditor(); }, self); return; }
    if (self->_cannedTarget == CannedTarget::Global && list->empty()) {
        // Deleting one of the shown defaults materialises the other seven.
        for (int i = 1; i <= 8; i++) {
            if (i - 1 == idx) continue;
            char key[12]; snprintf(key, sizeof(key), "canned_%d", i);
            list->push_back(t(key));
        }
    } else if ((size_t)idx < list->size()) {
        // Emptying a per-conversation list reverts that conversation to the global one.
        list->erase(list->begin() + idx);
    }
    self->syncCannedToRuntime();
    g_dsDirty = true;
    lv_async_call([](void* p) { ((SettingsScreen*)p)->hideCannedEditor(); }, self);
}

void SettingsScreen::convoEditorReadyCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_convoTextarea) return;
    String txt = convoTrim(String(lv_textarea_get_text(self->_convoTextarea)));
    auto& mgr = ConfigManager::instance();
    auto& cfg = mgr.config();
    auto commit = [self](bool ok, const char* errKey) {
        if (ok) { lv_async_call([](void* p){ auto* s=(SettingsScreen*)p; s->hideConvoEditor(); s->showConvoRebootConfirm(); }, self); }
        else    { UIManager::instance().showToast(t(errKey)); }
    };

    switch (self->_convoField) {
        case ConvoField::ContactAlias:
            if (txt.length() == 0) { UIManager::instance().showToast(t("chan_name_invalid")); return; }
            self->_convPendName = txt;
            self->setConvoField(ConvoField::ContactKey);
            break;
        case ConvoField::ContactKey: {
            txt.toLowerCase();
            if (!isHexN(txt, 64)) { UIManager::instance().showToast(t("err_bad_pubkey")); return; }
            if ((int)cfg.contacts.size() >= defaults::MAX_CHAT_CONTACTS) { UIManager::instance().showToast(t("err_at_cap")); return; }
            if (mgr.hasContactByPubkeyHex(txt)) { UIManager::instance().showToast(t("err_duplicate")); return; }
            ContactConfig cc; cc.alias = self->_convPendName; cc.publicKey = txt; cc.fromDiscovery = false;
            commit(mgr.appendDiscoveredContact(cc), "err_save_failed");
            break;
        }
        case ConvoField::ChanHashName: {
            String n; for (size_t i = 0; i < txt.length(); i++) { char c = tolower(txt[i]);
                if ((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-') n += c; }
            if (n.length() == 0) { UIManager::instance().showToast(t("chan_name_invalid")); return; }
            n = "#" + n;
            if ((int)cfg.channels.size() >= defaults::MAX_CHANNELS) { UIManager::instance().showToast(t("err_at_cap")); return; }
            ChannelConfig cc; cc.name = n; cc.type = "hashtag";
            commit(mgr.appendChannel(cc), "err_duplicate");
            break;
        }
        case ConvoField::ChanPrivName:
            if (txt.length() == 0) { UIManager::instance().showToast(t("chan_name_invalid")); return; }
            self->_convPendName = txt;
            self->setConvoField(ConvoField::ChanPrivPsk);
            break;
        case ConvoField::ChanPrivPsk: {
            txt.toLowerCase();
            if (!(isHexN(txt, 32) || isHexN(txt, 64))) { UIManager::instance().showToast(t("err_bad_psk")); return; }
            if ((int)cfg.channels.size() >= defaults::MAX_CHANNELS) { UIManager::instance().showToast(t("err_at_cap")); return; }
            ChannelConfig cc; cc.name = self->_convPendName; cc.type = "private"; cc.psk = txt;
            commit(mgr.appendChannel(cc), "err_duplicate");
            break;
        }
        case ConvoField::RoomName:
            if (txt.length() == 0) { UIManager::instance().showToast(t("chan_name_invalid")); return; }
            self->_convPendName = txt;
            self->setConvoField(ConvoField::RoomKey);
            break;
        case ConvoField::RoomKey:
            txt.toLowerCase();
            if (!isHexN(txt, 64)) { UIManager::instance().showToast(t("err_bad_pubkey")); return; }
            self->_convPendKey = txt;
            self->setConvoField(ConvoField::RoomPass);
            break;
        case ConvoField::RoomPass: {
            if ((int)cfg.roomServers.size() >= defaults::MAX_ROOM_SERVERS) { UIManager::instance().showToast(t("err_at_cap")); return; }
            RoomServerConfig rc; rc.name = self->_convPendName; rc.publicKey = self->_convPendKey; rc.password = txt;
            commit(mgr.appendRoom(rc), "err_duplicate");
            break;
        }
    }
}

void SettingsScreen::hide() {
    if (_screen) {
        // Commit batched edits on leave: one SD write, and reboot once if a
        // reboot-needing setting (theme/language/radio/gps) was changed.
        if (g_dsDirty) { ConfigManager::instance().save(); g_dsDirty = false; }
        if (g_dsReboot) { g_dsReboot = false; delay(200); ESP.restart(); }

        if (_themeBtnm) hideThemePicker();
        if (_nameTextarea) hideNameEditor();
        if (_scopeTextarea) hideScopeEditor();
        if (_langBtnm) hideLanguagePicker();
        if (_lockModeBtnm) hideLockModePicker();
        if (_autoLockBtnm) hideAutoLockPicker();
        if (_pinTextarea) hidePinEditor();
        if (_sosKeywordTextarea) hideSosKeywordEditor();
        if (_bootTextTextarea) hideBootTextEditor();
        if (_choicePanel) hideChoicePicker();
        if (_timezoneTextarea) hideTimezoneEditor();
        if (_convoTextarea) hideConvoEditor();
        if (_cannedTextarea) hideCannedEditor();
        if (_convoModalBtnm) hideButtonModal();
        lv_group_t* grp = lv_group_get_default();
        if (grp) {
            lv_group_set_editing(grp, false);
            lv_group_remove_obj(_backBtn);
            lv_group_remove_obj(_content);
        }
        // _heardCountLabel lives in a row that show() recreates each visit, so the
        // pointer dies on rebuild; drop it to avoid a dangling deref from tick().
        _heardCountLabel = nullptr;
        // Reset scroll so the NEXT entry starts at the top.
        lv_obj_scroll_to_y(_content, 0, LV_ANIM_OFF);
        lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsScreen::tick() {
    // Scope-list request timeout (issue #45). Checked before the section/visibility
    // guards below: the request runs over a modal that can be opened from the Radio,
    // Channels or Rooms sections, so it must fire regardless of the current section.
    if (_scopeReqDialog && _scopeReqExpiry && (int32_t)(millis() - _scopeReqExpiry) >= 0) {
        lv_obj_t* d = _scopeReqDialog;
        _scopeReqDialog = nullptr;
        _scopeReqExpiry = 0;
        MeshManager::instance().clearAnonReq();   // free the radio for an immediate retry
        ModalDialog::close(d);
        scopeInfo(t("scope_req_timeout"));
    }

    if (!_screen || lv_obj_has_flag(_screen, LV_OBJ_FLAG_HIDDEN)) return;
    if (_section != SettingsSection::Radio) return;

    // Live Heard-Adverts count on the Radio screen.
    if (_heardCountLabel) {
        uint32_t v = HeardAdvertCache::instance().version();
        if (v != _heardCacheVersion) {
            _heardCacheVersion = v;
            char rowBuf[40];
            snprintf(rowBuf, sizeof(rowBuf), "%s (%d)",
                     t("heard_adverts_title"), HeardAdvertCache::instance().count());
            lv_label_set_text(_heardCountLabel, rowBuf);
        }
    }
}

void SettingsScreen::backBtnCb(lv_event_t* e) {
    UIManager::instance().showScreen(Screen::ADMIN);
}

void SettingsScreen::heardAdvertsRowCb(lv_event_t* e) {
    UIManager::instance().showScreen(Screen::HEARD_ADVERTS);
}

// ─────────────────────────── Offgrid confirm (Radio) ───────────────────────────

void SettingsScreen::offgridRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openButtonModal(ConvoModal::OffgridConfirm);
}

// ─────────────────────────── Generic bool toggles ───────────────────────────

void SettingsScreen::boolToggleCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    lv_obj_t* sw = lv_event_get_target(e);
    bool v = lv_obj_has_state(sw, LV_STATE_CHECKED);
    BoolField id = (BoolField)(intptr_t)lv_obj_get_user_data(sw);
    auto& c = ConfigManager::instance().config();
    switch (id) {
        case BoolField::SaveHistory:      c.messaging.saveHistory = v; break;
        case BoolField::RequestTelemetry: c.messaging.requestTelemetry = v; break;
        case BoolField::AutoTelemetry:    c.messaging.autoTelemetry = v; break;
        case BoolField::CannedMessages:   c.messaging.cannedMessages = v; break;
        case BoolField::AllowMute:        c.messaging.allowMute = v; break;
        case BoolField::ShowHopCount:     c.messaging.showHopCount = v; break;
        case BoolField::ShareContact:     c.messaging.shareContact = v; break;
        case BoolField::Reactions:        c.messaging.reactions = v; break;
        // Reboot to apply: ChatScreen is created once at boot (UIManager::init),
        // so its header/emoji-button labels hold the font pointer they were given
        // then. Rebuilt-per-show screens would follow the toggle, but chat would
        // keep the old font until restart, which reads as the setting half-working.
        case BoolField::ColorEmoji:
            c.display.colorEmoji = v;
            g_dsReboot = true;
            UIManager::instance().showToast(t("theme_apply_body"));
            break;
        // Turning a permission off tightens the device and cannot be undone from
        // it, so defer to the confirm dialog. Turning one back on is the safe
        // direction and applies immediately.
        case BoolField::PermConvMgmt:
            if (!v) {
                if (!self) return;
                self->_permPending = PermField::ConvMgmt;
                lv_async_call([](void* p){ ((SettingsScreen*)p)->openButtonModal(ConvoModal::PermLockConfirm); }, self);
                return;   // nothing written yet; the dialog decides
            }
            c.permissions.conversationManagement = true; break;
        case BoolField::PermCompanion:
            if (!v) {
                if (!self) return;
                self->_permPending = PermField::Companion;
                lv_async_call([](void* p){ ((SettingsScreen*)p)->openButtonModal(ConvoModal::PermLockConfirm); }, self);
                return;
            }
            c.permissions.companion = true; break;
        case BoolField::GpsEnabled:       c.gpsEnabled = v; break;  // unused (see gpsToggleCb)
    }
    g_dsDirty = true;
}

void SettingsScreen::gpsToggleCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    lv_obj_t* sw = lv_event_get_target(e);
    bool v = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ConfigManager::instance().config().gpsEnabled = v;
    g_dsDirty = true;
    g_dsReboot = true;  // GPS hardware (de)init happens at boot
    UIManager::instance().showToast(t("theme_apply_body"));
    // Rebuild so the dependent diagnostics/editors show/hide.
    if (self) lv_async_call([](void* p) {
        auto* s = (SettingsScreen*)p;
        if (s->_screen && !lv_obj_has_flag(s->_screen, LV_OBJ_FLAG_HIDDEN)) s->show();
    }, self);
}

// ─────────────────────────── Generic choice picker ───────────────────────────

namespace {
std::vector<String> g_choiceNames;   // option codes (string fields)
std::vector<int>    g_choiceVals;    // numeric values (precision/region idx)
std::vector<String> g_choiceLabels;
}  // namespace

void SettingsScreen::adminPinRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openPinEditor(PinTarget::AdminPin);
}

// Locking Admin is confirmed by typing the admin PIN rather than tapping a second
// dialog: a tap is easy to fat-finger, and typing proves the user actually knows
// the PIN before they start depending on it to get back in.
void SettingsScreen::adminLockRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    if (ConfigManager::instance().config().security.adminPin.length() < 4) {
        // No usable admin PIN yet -> setting one is the prerequisite, so send the
        // user there instead of arming a lockout they could not undo on-device.
        UIManager::instance().showToast(t("admin_need_pin"));
        lv_async_call([](void* p){ ((SettingsScreen*)p)->openPinEditor(PinTarget::AdminPin); }, self);
        return;
    }
    lv_async_call([](void* p){ ((SettingsScreen*)p)->openButtonModal(ConvoModal::AdminLockConfirm); }, self);
}

void SettingsScreen::permSettingsRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openChoicePicker(ChoiceField::PermSettings);
}

void SettingsScreen::locFormatRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openChoicePicker(ChoiceField::LocationFormat);
}
void SettingsScreen::showTelemetryRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openChoicePicker(ChoiceField::ShowTelemetry);
}
void SettingsScreen::locPrecisionRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openChoicePicker(ChoiceField::LocationPrecision);
}
void SettingsScreen::regionRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openChoicePicker(ChoiceField::RegionPreset);
}
void SettingsScreen::advertRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openChoicePicker(ChoiceField::AdvertInterval);
}
void SettingsScreen::pathHashRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openChoicePicker(ChoiceField::PathHashMode);
}

void SettingsScreen::openChoicePicker(ChoiceField f) {
    if (_choicePanel) return;
    _choiceField = f;

    g_choiceNames.clear(); g_choiceVals.clear(); g_choiceLabels.clear();

    switch (f) {
        case ChoiceField::PermSettings:
            g_choiceNames.push_back("full");       g_choiceLabels.push_back(t("perm_full"));
            g_choiceNames.push_back("restricted"); g_choiceLabels.push_back(t("perm_restricted"));
            g_choiceNames.push_back("none");       g_choiceLabels.push_back(t("perm_none"));
            break;
        case ChoiceField::LocationFormat:
            g_choiceNames.push_back("decimal"); g_choiceLabels.push_back(t("loc_decimal"));
            g_choiceNames.push_back("mgrs");    g_choiceLabels.push_back(t("loc_mgrs"));
            g_choiceNames.push_back("both");    g_choiceLabels.push_back(t("loc_both"));
            break;
        case ChoiceField::ShowTelemetry:
            g_choiceNames.push_back("none");     g_choiceLabels.push_back(t("off"));
            g_choiceNames.push_back("battery");  g_choiceLabels.push_back(t("tel_battery"));
            g_choiceNames.push_back("location"); g_choiceLabels.push_back(t("tel_location"));
            g_choiceNames.push_back("both");     g_choiceLabels.push_back(t("tel_both"));
            break;
        case ChoiceField::LocationPrecision: {
            const int vals[] = {0, 19, 16, 14, 12, 10, 32};
            for (int v : vals) { g_choiceVals.push_back(v); g_choiceLabels.push_back(locPrecisionLabel((uint8_t)v)); }
            break;
        }
        case ChoiceField::RegionPreset:
            for (size_t i = 0; i < RADIO_PRESET_COUNT; i++) {
                g_choiceVals.push_back((int)i);
                g_choiceLabels.push_back(RADIO_PRESETS[i].label);
            }
            break;
        case ChoiceField::AdvertInterval:
            for (uint16_t m : ADVERT_MINS) {
                g_choiceVals.push_back((int)m);
                g_choiceLabels.push_back(advertLabel(m));
            }
            break;
        case ChoiceField::PathHashMode:
            for (int m = 0; m <= 2; m++) {
                g_choiceVals.push_back(m);
                char b[12]; snprintf(b, sizeof(b), "%d B/hop", m + 1);
                g_choiceLabels.push_back(String(b));
            }
            break;
    }

    // Determine which option matches the current config value.
    uint16_t initSel = 0;
    const auto& cfg = ConfigManager::instance().config();
    switch (f) {
        case ChoiceField::PermSettings:
            for (size_t i = 0; i < g_choiceNames.size(); i++) {
                if (g_choiceNames[i] == cfg.permissions.settings) { initSel = (uint16_t)i; break; }
            }
            break;
        case ChoiceField::LocationFormat:
            for (size_t i = 0; i < g_choiceNames.size(); i++) {
                if (g_choiceNames[i] == cfg.messaging.locationFormat) { initSel = (uint16_t)i; break; }
            }
            break;
        case ChoiceField::ShowTelemetry:
            for (size_t i = 0; i < g_choiceNames.size(); i++) {
                if (g_choiceNames[i] == cfg.messaging.showTelemetry) { initSel = (uint16_t)i; break; }
            }
            break;
        case ChoiceField::LocationPrecision:
            for (size_t i = 0; i < g_choiceVals.size(); i++) {
                if (g_choiceVals[i] == (int)cfg.locationPrecision) { initSel = (uint16_t)i; break; }
            }
            break;
        case ChoiceField::RegionPreset: {
            int idx = matchRadioPreset(cfg.radio);
            if (idx >= 0) initSel = (uint16_t)idx;
            break;
        }
        case ChoiceField::AdvertInterval:
            for (size_t i = 0; i < g_choiceVals.size(); i++) {
                if (g_choiceVals[i] == (int)cfg.radio.advertIntervalMin) { initSel = (uint16_t)i; break; }
            }
            break;
        case ChoiceField::PathHashMode:
            for (size_t i = 0; i < g_choiceVals.size(); i++) {
                if (g_choiceVals[i] == (int)cfg.radio.pathHashMode) { initSel = (uint16_t)i; break; }
            }
            break;
    }

    // Overlay — dim background, click to cancel.
    _choiceOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_choiceOverlay, Display::width(), Display::height());
    lv_obj_set_pos(_choiceOverlay, 0, 0);
    lv_obj_set_style_bg_color(_choiceOverlay, theme::SCRIM(), 0);
    lv_obj_set_style_bg_opa(_choiceOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_choiceOverlay, 0, 0);
    lv_obj_clear_flag(_choiceOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_choiceOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_choiceOverlay, [](lv_event_t* ev) {
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* ctx) { ((SettingsScreen*)ctx)->hideChoicePicker(); }, s);
    }, LV_EVENT_CLICKED, this);

    // Panel — flex column: roller + button row.
    _choicePanel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_choicePanel, theme::MODAL_TEXT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_align(_choicePanel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_choicePanel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(_choicePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_choicePanel, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(_choicePanel, 1, 0);
    lv_obj_set_style_radius(_choicePanel, 8, 0);
    lv_obj_set_style_pad_all(_choicePanel, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_row(_choicePanel, theme::PAD_SMALL, 0);
    lv_obj_set_flex_flow(_choicePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(_choicePanel, LV_OBJ_FLAG_SCROLLABLE);

    // Build newline-separated options string for the roller.
    String opts;
    for (size_t i = 0; i < g_choiceLabels.size(); i++) {
        if (i > 0) opts += "\n";
        opts += g_choiceLabels[i];
    }

    _choiceRoller = lv_roller_create(_choicePanel);
    lv_roller_set_options(_choiceRoller, opts.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(_choiceRoller, initSel, LV_ANIM_OFF);
    lv_obj_set_width(_choiceRoller, LV_PCT(100));
    lv_obj_set_style_text_font(_choiceRoller, FONT_HEADING, 0);
    lv_obj_set_style_text_color(_choiceRoller, theme::TEXT_PRIMARY(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_choiceRoller, theme::BG_SECONDARY(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_choiceRoller, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_choiceRoller, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_choiceRoller, theme::ACCENT(), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(_choiceRoller, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(_choiceRoller, theme::TEXT_ON_ACCENT(), LV_PART_SELECTED);
#ifdef PLATFORM_TWATCH
    lv_roller_set_visible_row_count(_choiceRoller, 3);
#else
    lv_roller_set_visible_row_count(_choiceRoller, 5);
#endif

    // Button row — OK (confirm) and Cancel.
    lv_obj_t* btnRow = lv_obj_create(_choicePanel);
    lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* okBtn = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(okBtn, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(okBtn, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(okBtn, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(okBtn, 1, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(okBtn, choiceChosenCb, LV_EVENT_CLICKED, this);
    lv_obj_t* okLbl = lv_label_create(okBtn);
    lv_label_set_text(okLbl, t("btn_save"));
    lv_obj_set_style_text_font(okLbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(okLbl, theme::TEXT_ON_ACCENT(), 0);
    lv_obj_center(okLbl);

    lv_obj_t* cancelBtn = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(cancelBtn, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_border_color(cancelBtn, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(cancelBtn, 1, 0);
    lv_obj_set_style_bg_color(cancelBtn, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancelBtn, [](lv_event_t* ev) {
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* ctx) { ((SettingsScreen*)ctx)->hideChoicePicker(); }, s);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* cxlLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_set_style_text_font(cxlLbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(cxlLbl, theme::TEXT_PRIMARY(), 0);
    lv_obj_center(cxlLbl);

    // Encoder group: roller (enter editing mode to scroll) → OK → Cancel.
    lv_group_t* g = lv_group_create();
    _editorGroup = g;
    lv_group_add_obj(g, _choiceRoller);
    lv_group_add_obj(g, okBtn);
    lv_group_add_obj(g, cancelBtn);
    lv_group_focus_obj(_choiceRoller);

    UIManager::instance().switchToModalGroup(_choicePanel);
    IInput::instance().attachToGroup(g);
}

void SettingsScreen::choiceChosenCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_choiceRoller) return;
    uint16_t idx = lv_roller_get_selected(self->_choiceRoller);

    auto& c = ConfigManager::instance().config();
    switch (self->_choiceField) {
        case ChoiceField::PermSettings:
            if (idx < g_choiceNames.size() && c.permissions.settings != g_choiceNames[idx]) {
                // The row is only editable while "full", so any change here tightens.
                // Confirm first: once applied these rows go read-only and the device
                // cannot restore them itself.
                self->_permPending      = PermField::Settings;
                self->_permPendingValue = g_choiceNames[idx];
            }
            break;
        case ChoiceField::LocationFormat:
            if (idx < g_choiceNames.size() && c.messaging.locationFormat != g_choiceNames[idx]) {
                c.messaging.locationFormat = g_choiceNames[idx]; g_dsDirty = true;
            }
            break;
        case ChoiceField::ShowTelemetry:
            if (idx < g_choiceNames.size() && c.messaging.showTelemetry != g_choiceNames[idx]) {
                c.messaging.showTelemetry = g_choiceNames[idx]; g_dsDirty = true;
            }
            break;
        case ChoiceField::LocationPrecision:
            if (idx < g_choiceVals.size() && c.locationPrecision != (uint8_t)g_choiceVals[idx]) {
                c.locationPrecision = (uint8_t)g_choiceVals[idx]; g_dsDirty = true;
            }
            break;
        case ChoiceField::AdvertInterval:
            if (idx < g_choiceVals.size() && c.radio.advertIntervalMin != (uint16_t)g_choiceVals[idx]) {
                c.radio.advertIntervalMin = (uint16_t)g_choiceVals[idx];
                g_dsDirty = true;
                g_dsReboot = true;
                UIManager::instance().showToast(t("theme_apply_body"));
            }
            break;
        case ChoiceField::RegionPreset:
            if (idx < g_choiceVals.size()) {
                RadioConfig before = c.radio;
                applyRadioPreset(c.radio, (size_t)g_choiceVals[idx]);
                if (before.frequency != c.radio.frequency ||
                    before.spreadingFactor != c.radio.spreadingFactor ||
                    before.bandwidth != c.radio.bandwidth ||
                    before.codingRate != c.radio.codingRate ||
                    before.txPower != c.radio.txPower) {
                    g_dsDirty = true;
                    g_dsReboot = true;
                    UIManager::instance().showToast(t("theme_apply_body"));
                }
            }
            break;
        case ChoiceField::PathHashMode:
            if (idx < g_choiceVals.size() && c.radio.pathHashMode != (uint8_t)g_choiceVals[idx]) {
                c.radio.pathHashMode = (uint8_t)g_choiceVals[idx];
                g_dsDirty = true;
                g_dsReboot = true;
                UIManager::instance().showToast(t("theme_apply_body"));
            }
            break;
    }
    lv_async_call([](void* p) { ((SettingsScreen*)p)->hideChoicePicker(); }, self);
}

void SettingsScreen::hideChoicePicker() {
    if (!_choicePanel) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
    _choiceRoller = nullptr;
    lv_obj_del_async(_choicePanel);   _choicePanel   = nullptr;
    lv_obj_del_async(_choiceOverlay); _choiceOverlay = nullptr;
    if (_screen) show();
    // A pending permission tightening waits until the picker is torn down, so the
    // confirm dialog is not stacked on top of a modal that is being deleted.
    if (_permPending != PermField::None)
        lv_async_call([](void* p){ ((SettingsScreen*)p)->openButtonModal(ConvoModal::PermLockConfirm); }, this);
}

// ─────────────────────────── Timezone editor (GPS) ───────────────────────────

void SettingsScreen::timezoneReadyCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_timezoneTextarea) return;
    const char* text = lv_textarea_get_text(self->_timezoneTextarea);
    String newTz = text ? String(text) : String("");
    {
        const char* s = newTz.c_str();
        int len = strlen(s);
        int l = 0, r = len - 1;
        while (l <= r && isspace((unsigned char)s[l])) ++l;
        while (r >= l && isspace((unsigned char)s[r])) --r;
        if (l > 0 || r < len - 1) newTz = newTz.substring(l, r + 1);
    }
    if (newTz.length() > 48) newTz = newTz.substring(0, 48);
    auto& mgr = ConfigManager::instance();
    if (mgr.config().gpsTimezone != newTz) {
        mgr.config().gpsTimezone = newTz;
        g_dsDirty = true;
    }
    lv_async_call([](void* p) { ((SettingsScreen*)p)->hideTimezoneEditor(); }, self);
}

void SettingsScreen::timezoneRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_timezoneTextarea) return;
    const auto& cfg = ConfigManager::instance().config();

    self->_timezoneOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_timezoneOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_timezoneOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_timezoneOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(self->_timezoneOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(self->_timezoneOverlay, 0, 0);
    lv_obj_clear_flag(self->_timezoneOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(self->_timezoneOverlay);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(lbl, t("lbl_timezone"));
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    self->_timezoneTextarea = lv_textarea_create(self->_timezoneOverlay);
    lv_textarea_set_one_line(self->_timezoneTextarea, true);
    lv_textarea_set_max_length(self->_timezoneTextarea, 48);
    lv_textarea_set_placeholder_text(self->_timezoneTextarea, "CET-1CEST,M3.5.0/2,M10.5.0/3");
    lv_textarea_set_text(self->_timezoneTextarea, cfg.gpsTimezone.c_str());
    lv_obj_set_width(self->_timezoneTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(self->_timezoneTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(self->_timezoneTextarea, theme::ACCENT(), LV_STATE_FOCUSED);

    lv_obj_t* btnRow = lv_obj_create(self->_timezoneOverlay);
    lv_obj_set_size(btnRow, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btnRow, theme::PAD_SMALL, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_width(save, LV_PCT(100));
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, timezoneReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_width(cancel, LV_PCT(100));
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((SettingsScreen*)p)->hideTimezoneEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    self->_editorGroup = g;
    lv_group_add_obj(g, self->_timezoneTextarea);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(self->_timezoneTextarea);
    UIManager::instance().switchToModalGroup(self->_timezoneOverlay);
    IInput::instance().attachToGroup(g);
    lv_obj_add_event_cb(self->_timezoneTextarea, timezoneReadyCb, LV_EVENT_READY, self);

#ifdef PLATFORM_TWATCH
    self->_timezoneKbd = lv_keyboard_create(self->_timezoneOverlay);
    lv_keyboard_set_textarea(self->_timezoneKbd, self->_timezoneTextarea);
    lv_keyboard_set_popovers(self->_timezoneKbd, true);
    lv_btnmatrix_set_btn_ctrl_all(self->_timezoneKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(self->_timezoneKbd, timezoneReadyCb, LV_EVENT_READY, self);
    lv_obj_add_event_cb(self->_timezoneKbd, [](lv_event_t* ev) {
        auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_timezoneKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((SettingsScreen*)p)->hideTimezoneEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

void SettingsScreen::hideTimezoneEditor() {
    if (!_timezoneTextarea) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
#ifdef PLATFORM_TWATCH
    _timezoneKbd = nullptr;
#endif
    _timezoneTextarea = nullptr;
    lv_obj_del_async(_timezoneOverlay);
    _timezoneOverlay = nullptr;
    if (_screen) show();
}

// ─────────────────────────── Security pickers / PIN ───────────────────────────

// Lock-mode / auto-lock chooser state. File-scope for the static callbacks:
namespace {
std::vector<String>      g_lockModeNames;
std::vector<String>      g_lockModeLabels;
std::vector<const char*> g_lockModeMap;

std::vector<String>      g_autoLockNames;
std::vector<String>      g_autoLockLabels;
std::vector<const char*> g_autoLockMap;
}  // namespace

void SettingsScreen::lockModeRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
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

    lv_obj_add_event_cb(self->_lockModeBtnm, lockModeChosenCb, LV_EVENT_CLICKED, self);
    UIManager::instance().switchToModalGroup(self->_lockModeBtnm);
}

void SettingsScreen::autoLockRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
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

    lv_obj_add_event_cb(self->_autoLockBtnm, autoLockChosenCb, LV_EVENT_CLICKED, self);  // see lockModeRowCb
    UIManager::instance().switchToModalGroup(self->_autoLockBtnm);
}

void SettingsScreen::pinRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (self) self->openPinEditor();
}

// Body split out of pinRowCb so the Lock Mode picker can open the editor too,
// when "pin" is chosen without a usable PIN.
void SettingsScreen::openPinEditor(PinTarget target) {
    SettingsScreen* self = this;
    self->_pinTarget = target;
    self->_pendingPin = "";
    self->_pinAwaitRepeat = false;
    if (self->_pinTextarea) return;
    const auto& cfg = ConfigManager::instance().config();

    self->_pinOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_pinOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_pinOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_pinOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(self->_pinOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(self->_pinOverlay, 0, 0);
    lv_obj_clear_flag(self->_pinOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(self->_pinOverlay);
    self->_pinTitleLbl = lbl;
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(lbl, target == PinTarget::AdminPin ? t("lbl_admin_pin") : t("lbl_pin_code"));
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    self->_pinTextarea = lv_textarea_create(self->_pinOverlay);
    lv_textarea_set_one_line(self->_pinTextarea, true);
    lv_textarea_set_max_length(self->_pinTextarea, 8);
    lv_textarea_set_placeholder_text(self->_pinTextarea, t("lbl_pin_code"));
    lv_textarea_set_text(self->_pinTextarea,
        (target == PinTarget::AdminPin ? cfg.security.adminPin : cfg.security.pinCode).c_str());
    lv_obj_set_width(self->_pinTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(self->_pinTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(self->_pinTextarea, theme::ACCENT(), LV_STATE_FOCUSED);

    lv_obj_t* btnRow = lv_obj_create(self->_pinOverlay);
    lv_obj_set_size(btnRow, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btnRow, theme::PAD_SMALL, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_width(save, LV_PCT(100));
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, pinReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_width(cancel, LV_PCT(100));
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((SettingsScreen*)p)->hidePinEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    self->_editorGroup = g;
    lv_group_add_obj(g, self->_pinTextarea);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(self->_pinTextarea);
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
        auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_pinKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((SettingsScreen*)p)->hidePinEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

void SettingsScreen::pinReadyCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_pinTextarea) return;
    const char* text = lv_textarea_get_text(self->_pinTextarea);
    String newPin = text ? String(text) : String("");
    const char* s = newPin.c_str();
    int len = strlen(s);
    int l = 0, r = len - 1;
    while (l <= r && isspace((unsigned char)s[l])) ++l;
    while (r >= l && isspace((unsigned char)s[r])) --r;
    if (l > 0 || r < len - 1) {
        newPin = newPin.substring(l, r + 1);
    }
    if (newPin.length() > 8) newPin = newPin.substring(0, 8);

    // Every lock path requires >= 4 (UIManager::updateKeyLockToggle and the
    // auto-lock branch), so a shorter PIN saves fine and then does nothing at
    // all -- which is exactly how someone ends up with "PIN mode" that only ever
    // engages the key lock. Refuse it here instead. Empty is still allowed: that
    // is how the PIN is cleared.
    if (newPin.length() > 0 && newPin.length() < 4) {
        UIManager::instance().showToast(t("pin_too_short"));
        self->_pendingPin = "";
        lv_textarea_set_text(self->_pinTextarea, "");
        lv_textarea_set_placeholder_text(self->_pinTextarea, t("lbl_pin_code"));
        return;
    }

    auto& mgr = ConfigManager::instance();
    if (!self->_pinAwaitRepeat) {
        // First entry accepted -> ask for it again, and SAY that is what is
        // happening. Previously both steps were captioned identically, so the
        // second prompt looked like the first one had simply failed.
        self->_pendingPin     = newPin;
        self->_pinAwaitRepeat = true;
        lv_textarea_set_text(self->_pinTextarea, "");
        lv_textarea_set_placeholder_text(self->_pinTextarea, t("pin_repeat"));
        if (self->_pinTitleLbl) lv_label_set_text(self->_pinTitleLbl, t("pin_repeat"));
        return;
    }
    if (self->_pendingPin == newPin) {
        String& slot = (self->_pinTarget == PinTarget::AdminPin)
                         ? mgr.config().security.adminPin
                         : mgr.config().security.pinCode;
        if (slot != newPin) {
            slot = newPin;
            g_dsDirty = true;
        }
        // Clearing the screen PIN while Lock Mode still says "pin" would recreate
        // the state the Lock Mode guard exists to prevent: a mode that needs a PIN,
        // with no PIN, silently degrading to key lock. Guard both directions. The
        // admin PIN needs no equivalent -- Lock Admin refuses to arm without one,
        // and once Admin is locked this screen is unreachable anyway.
        bool reverted = false;
        if (self->_pinTarget == PinTarget::ScreenPin && newPin.length() == 0) {
            auto& sec = mgr.config().security;
            if (sec.lockMode == "pin") { sec.lockMode = "key"; reverted = true; }
            if (sec.autoLock == "pin") { sec.autoLock = "key"; reverted = true; }
            if (reverted) g_dsDirty = true;
        }
        self->_pendingPin     = "";
        self->_pinAwaitRepeat = false;
        UIManager::instance().showToast(reverted ? t("pin_mode_reverted")
                                                 : (newPin.length() ? t("pin_saved") : t("pin_cleared")));
        lv_async_call([](void* p) { ((SettingsScreen*)p)->hidePinEditor(); }, self);
        return;
    }
    // Mismatch. Say so -- this used to silently wipe the field and start over,
    // which reads as the editor randomly refusing to accept anything.
    UIManager::instance().showToast(t("pin_mismatch"));
    self->_pendingPin     = "";
    self->_pinAwaitRepeat = false;
    lv_textarea_set_text(self->_pinTextarea, "");
    lv_textarea_set_placeholder_text(self->_pinTextarea, t("lbl_pin_code"));
    if (self->_pinTitleLbl) {
        lv_label_set_text(self->_pinTitleLbl,
            self->_pinTarget == PinTarget::AdminPin ? t("lbl_admin_pin") : t("lbl_pin_code"));
    }
}

void SettingsScreen::lockModeChosenCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_lockModeBtnm) return;
    uint16_t idx = lv_btnmatrix_get_selected_btn(self->_lockModeBtnm);
    if (idx == LV_BTNMATRIX_BTN_NONE) return;
    if (idx < g_lockModeNames.size()) {
        auto& mgr = ConfigManager::instance();
        String newMode = g_lockModeNames[idx];
        if (mgr.config().security.lockMode != newMode) {
            mgr.config().security.lockMode = newMode;
            g_dsDirty = true;
        }
        // Picking PIN without a usable PIN would silently behave as key lock.
        // Send the user straight to the PIN editor; hidePinEditor() reverts the
        // mode if they leave without setting one.
        if (newMode == "pin" && mgr.config().security.pinCode.length() < 4) {
            self->_pinModePending = true;
            lv_async_call([](void* p) {
                auto* sc = (SettingsScreen*)p;
                sc->hideLockModePicker();
                sc->openPinEditor();
            }, self);
            return;
        }
    }
    lv_async_call([](void* p) { ((SettingsScreen*)p)->hideLockModePicker(); }, self);
}

void SettingsScreen::autoLockChosenCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_autoLockBtnm) return;
    uint16_t idx = lv_btnmatrix_get_selected_btn(self->_autoLockBtnm);
    if (idx == LV_BTNMATRIX_BTN_NONE) return;
    if (idx < g_autoLockNames.size()) {
        auto& mgr = ConfigManager::instance();
        String newMode = g_autoLockNames[idx];
        if (mgr.config().security.autoLock != newMode) {
            mgr.config().security.autoLock = newMode;
            g_dsDirty = true;
        }
    }
    lv_async_call([](void* p) { ((SettingsScreen*)p)->hideAutoLockPicker(); }, self);
}

void SettingsScreen::hideLockModePicker() {
    if (!_lockModeBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
    lv_obj_del_async(_lockModeBtnm);
    _lockModeBtnm = nullptr;
    if (_screen) show();
}

void SettingsScreen::hideAutoLockPicker() {
    if (!_autoLockBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
    lv_obj_del_async(_autoLockBtnm);
    _autoLockBtnm = nullptr;
    if (_screen) show();
}

void SettingsScreen::hidePinEditor() {
    if (!_pinTextarea) return;
    // Opened automatically by a Lock Mode change and still no usable PIN -> fall
    // back to key lock rather than leaving "pin" set but inert.
    if (_pinModePending && _pinTarget == PinTarget::ScreenPin) {
        _pinModePending = false;
        auto& mgr = ConfigManager::instance();
        if (mgr.config().security.pinCode.length() < 4 && mgr.config().security.lockMode == "pin") {
            mgr.config().security.lockMode = "key";
            g_dsDirty = true;
            UIManager::instance().showToast(t("pin_mode_reverted"));
        }
    }
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
#ifdef PLATFORM_TWATCH
    _pinKbd = nullptr;
#endif
    _pinTextarea  = nullptr;
    _pinTitleLbl  = nullptr;
    lv_obj_del_async(_pinOverlay);
    _pinOverlay = nullptr;
    if (_screen) show();
}

// ─────────────────────────── Sound / battery toggles ───────────────────────────

void SettingsScreen::soundToggleCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    bool newVal = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.config().soundEnabled = newVal;
    g_dsDirty = true;
    Speaker::instance().setSoundEnabled(newVal);
    if (self) lv_async_call([](void* p) {
        auto* s = (SettingsScreen*)p;
        if (s->_screen && !lv_obj_has_flag(s->_screen, LV_OBJ_FLAG_HIDDEN)) s->show();
    }, self);
}

void SettingsScreen::lowAlertToggleCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    bool newVal = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.config().battery.lowAlertEnabled = newVal;
    g_dsDirty = true;
    if (self) lv_async_call([](void* p) {
        auto* s = (SettingsScreen*)p;
        if (s->_screen && !lv_obj_has_flag(s->_screen, LV_OBJ_FLAG_HIDDEN)) s->show();
    }, self);
}

// ─────────────────────────── SOS keyword editor ───────────────────────────

void SettingsScreen::sosKeywordReadyCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
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
        g_dsDirty = true;
    }
    lv_async_call([](void* p) { ((SettingsScreen*)p)->hideSosKeywordEditor(); }, self);
}

void SettingsScreen::sosKeywordRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
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
    lv_obj_set_size(btnRow, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btnRow, theme::PAD_SMALL, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_width(save, LV_PCT(100));
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, sosKeywordReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_width(cancel, LV_PCT(100));
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((SettingsScreen*)p)->hideSosKeywordEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    self->_editorGroup = g;
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
        auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_sosKeywordKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((SettingsScreen*)p)->hideSosKeywordEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

void SettingsScreen::hideSosKeywordEditor() {
    if (!_sosKeywordTextarea) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
#ifdef PLATFORM_TWATCH
    _sosKeywordKbd = nullptr;
#endif
    _sosKeywordTextarea = nullptr;
    lv_obj_del_async(_sosKeywordOverlay);
    _sosKeywordOverlay = nullptr;
    if (_screen) show();
}

// ─────────────────────────── Theme picker ───────────────────────────

namespace {
std::vector<String>      g_themeNames;
std::vector<String>      g_themeLabels;
std::vector<const char*> g_themeMap;
}  // namespace

void SettingsScreen::themeRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
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
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* ctx) { ((SettingsScreen*)ctx)->hideThemePicker(); }, s);
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
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(ev);
        uint16_t idx = lv_btnmatrix_get_selected_btn(s->_themeBtnm);
        if (idx == LV_BTNMATRIX_BTN_NONE) return;
        if (idx < g_themeNames.size()) {
            auto& mgr = ConfigManager::instance();
            if (mgr.config().display.theme != g_themeNames[idx]) {
                mgr.config().display.theme = g_themeNames[idx];
                g_dsDirty = true;
                g_dsReboot = true;
                UIManager::instance().showToast(t("theme_apply_body"));
            }
        }
        lv_async_call([](void* ctx) { ((SettingsScreen*)ctx)->hideThemePicker(); }, s);
    }, LV_EVENT_CLICKED, self);

    UIManager::instance().switchToModalGroup(self->_themeBtnm);
}

void SettingsScreen::hideThemePicker() {
    if (!_themeBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
    lv_obj_del_async(_themeBtnm);    _themeBtnm = nullptr;
    lv_obj_del_async(_themeOverlay); _themeOverlay = nullptr;
    if (_screen) show();
}

// ─────────────────────────── Device name editor ───────────────────────────

void SettingsScreen::hideNameEditor() {
    if (!_nameTextarea) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
#ifdef PLATFORM_TWATCH
    _nameKbd = nullptr;
#endif
    _nameTextarea = nullptr;
    lv_obj_del_async(_nameOverlay);
    _nameOverlay = nullptr;
    if (_screen) show();
}

void SettingsScreen::nameReadyCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_nameTextarea) return;
    const char* text = lv_textarea_get_text(self->_nameTextarea);
    String newName = text ? String(text) : String("");
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
        g_dsDirty = true;
    }
    lv_async_call([](void* p) { ((SettingsScreen*)p)->hideNameEditor(); }, self);
}

void SettingsScreen::nameRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
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

    lv_obj_t* btnRow = lv_obj_create(self->_nameOverlay);
    lv_obj_set_size(btnRow, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btnRow, theme::PAD_SMALL, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_width(save, LV_PCT(100));
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, nameReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_width(cancel, LV_PCT(100));
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((SettingsScreen*)p)->hideNameEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    self->_editorGroup = g;
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
        auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_nameKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((SettingsScreen*)p)->hideNameEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

// ─────────────────────────── Radio region/scope editor ───────────────────────────
// Mirrors the device-name editor. On save it reboots-on-leave (g_dsReboot) because the
// global transport scope re-derives at boot (scopeToTransportKey -> _globalScope).

void SettingsScreen::hideScopeEditor() {
    if (!_scopeTextarea) return;
    // Drop any pending scope-list request state (#45) so a late reply is ignored once
    // the editor (and its group) are gone. No dialog is up here — the picker modals hold
    // input focus while open, so hide can only run with the plain editor showing.
    _scopeReqDialog = nullptr;
    _scopeReqExpiry = 0;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
#ifdef PLATFORM_TWATCH
    _scopeKbd = nullptr;
#endif
    _scopeTextarea = nullptr;
    lv_obj_del_async(_scopeOverlay);
    _scopeOverlay = nullptr;
    if (_screen) show();
}

void SettingsScreen::scopeReadyCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_scopeTextarea) return;
    String s = convoTrim(String(lv_textarea_get_text(self->_scopeTextarea)));
    sanitizeScope(s);   // a scope is one region name — drop anything past a space (#36)
    auto& cfg = ConfigManager::instance().config();
    int idx = self->_scopeTargetIdx;
    String* target = nullptr;
    if (self->_scopeTarget == ScopeTarget::Global) {
        if (s.length() == 0) s = "*";              // global blank == "*" == no scope
        target = &cfg.radio.scope;
    } else if (self->_scopeTarget == ScopeTarget::Channel && (size_t)idx < cfg.channels.size()) {
        target = &cfg.channels[idx].scope;          // per-channel: blank == "" == inherit global
    } else if (self->_scopeTarget == ScopeTarget::Room && (size_t)idx < cfg.roomServers.size()) {
        target = &cfg.roomServers[idx].scope;       // per-room: blank == "" == inherit global
    }
    if (target && *target != s) {
        *target = s; g_dsDirty = true; g_dsReboot = true;
        UIManager::instance().showToast(t("theme_apply_body"));
    }
    lv_async_call([](void* p) { ((SettingsScreen*)p)->hideScopeEditor(); }, self);
}

// Radio-screen row → global scope.
void SettingsScreen::scopeRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    self->_scopeTarget = ScopeTarget::Global;
    self->openScopeEditor();
}

// Builds the scope editor for the current _scopeTarget / _scopeTargetIdx.
void SettingsScreen::openScopeEditor() {
    if (_scopeTextarea) return;
    const auto& cfg = ConfigManager::instance().config();
    String preFill;
    if (_scopeTarget == ScopeTarget::Channel && (size_t)_scopeTargetIdx < cfg.channels.size())
        preFill = cfg.channels[_scopeTargetIdx].scope;
    else if (_scopeTarget == ScopeTarget::Room && (size_t)_scopeTargetIdx < cfg.roomServers.size())
        preFill = cfg.roomServers[_scopeTargetIdx].scope;
    else
        preFill = cfg.radio.scope;

    _scopeOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_scopeOverlay, Display::width(), Display::height());
    lv_obj_set_pos(_scopeOverlay, 0, 0);
    lv_obj_set_style_bg_color(_scopeOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(_scopeOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_scopeOverlay, 0, 0);
    lv_obj_clear_flag(_scopeOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(_scopeOverlay);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(lbl, t("lbl_scope"));
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    _scopeTextarea = lv_textarea_create(_scopeOverlay);
    lv_textarea_set_one_line(_scopeTextarea, true);
    lv_textarea_set_max_length(_scopeTextarea, 31);
    lv_textarea_set_placeholder_text(_scopeTextarea, t("scope_hint"));
    lv_textarea_set_text(_scopeTextarea, preFill.c_str());
    lv_obj_set_width(_scopeTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(_scopeTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(_scopeTextarea, theme::ACCENT(), LV_STATE_FOCUSED);

    lv_obj_t* btnRow = lv_obj_create(_scopeOverlay);
    lv_obj_set_size(btnRow, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btnRow, theme::PAD_SMALL, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    // "From repeater" (issue #45): a shortcut that fills the textarea from a nearby
    // repeater's advertised region list. Manual entry above stays fully usable. On its
    // own full-width row so the three actions fit on the 240px-tall screen.
    lv_obj_t* fromRep = lv_btn_create(btnRow);
    lv_obj_set_width(fromRep, LV_PCT(100));
    lv_obj_set_style_bg_color(fromRep, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(fromRep, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(fromRep, [](lv_event_t* ev) {
        auto* s = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((SettingsScreen*)p)->onScopeFromRepeater(); }, s);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* frLbl = lv_label_create(fromRep);
    lv_label_set_text(frLbl, t("scope_from_repeater"));
    lv_obj_center(frLbl);

    // Save + Cancel share one horizontal row.
    lv_obj_t* actionRow = lv_obj_create(btnRow);
    lv_obj_set_size(actionRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actionRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actionRow, 0, 0);
    lv_obj_set_style_pad_all(actionRow, 0, 0);
    lv_obj_set_flex_flow(actionRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actionRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actionRow, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(actionRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(actionRow);
    lv_obj_set_flex_grow(save, 1);
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, scopeReadyCb, LV_EVENT_CLICKED, this);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    lv_obj_t* cancel = lv_btn_create(actionRow);
    lv_obj_set_flex_grow(cancel, 1);
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((SettingsScreen*)p)->hideScopeEditor(); }, s);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    _editorGroup = g;
    lv_group_add_obj(g, _scopeTextarea);
    lv_group_add_obj(g, fromRep);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(_scopeTextarea);
    UIManager::instance().switchToModalGroup(_scopeOverlay);
    IInput::instance().attachToGroup(g);
    lv_obj_add_event_cb(_scopeTextarea, scopeReadyCb, LV_EVENT_READY, this);

#ifdef PLATFORM_TWATCH
    _scopeKbd = lv_keyboard_create(_scopeOverlay);
    lv_keyboard_set_textarea(_scopeKbd, _scopeTextarea);
    lv_keyboard_set_popovers(_scopeKbd, true);
    lv_btnmatrix_set_btn_ctrl_all(_scopeKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(_scopeKbd, scopeReadyCb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(_scopeKbd, [](lv_event_t* ev) {
        auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_scopeKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((SettingsScreen*)p)->hideScopeEditor(); }, self);
        }
    }, LV_EVENT_ALL, this);
#endif
}

// ───────────────── "Get scope from repeater" picker (issue #45) ─────────────────
//
// Additive to manual entry, and single-select: the repeater reports its region list and
// the user picks exactly ONE, which fills the editor textarea (still reviewable before Save).
// The scope editor overlay stays OPEN underneath — the roller's dim scrim shows the typed
// text behind it. switchToModalGroup does not stack, but the roller uses its own dedicated
// group, so the editor's _editorGroup survives; returnToScopeEditor() re-attaches input to
// it when a picker/popup closes. Repeater and scope selection both use a scrollable roller
// (not a button list, which overflowed the 240px screen). Applies identically to global,
// per-channel and per-room scope, since all three share this editor via _scopeTarget.
namespace { constexpr uint8_t SCOPE_ADV_TYPE_REPEATER = 2; }  // mirrors ADV_TYPE_REPEATER

// Re-attach input to the still-open scope editor after a picker/popup closes.
void SettingsScreen::returnToScopeEditor() {
    if (_editorGroup) {
        IInput::instance().attachToGroup(_editorGroup);
        if (_scopeTextarea) lv_group_focus_obj(_scopeTextarea);
    }
}

// One-button info popup (no repeaters / busy / failed / timeout / no named regions),
// then back to the editor.
void SettingsScreen::scopeInfo(const String& msg) {
    ModalDialog::show(t("scope_from_repeater"), msg, { String(t("btn_ok")) },
        [this](lv_obj_t* dlg, int) { ModalDialog::close(dlg); returnToScopeEditor(); });
}

// "From repeater" tapped — snapshot heard repeaters and open the roller over the editor.
void SettingsScreen::onScopeFromRepeater() {
    if (!_scopeTextarea) return;   // editor gone

    _scopeRepeaters.clear();
    const auto& cache = HeardAdvertCache::instance();
    for (int i = 0; i < cache.count(); i++) {
        const HeardAdvert& e = cache.entries()[i];
        if (e.type != SCOPE_ADV_TYPE_REPEATER) continue;
        if (e.hops != 0) continue;   // zero-hop only: the query can't reach multi-hop repeaters
        ScopeRepeater r;
        memcpy(r.pubKey, e.pubKey, 32);
        r.name = e.name[0] ? String(e.name) : String(t("scope_unnamed_repeater"));
        _scopeRepeaters.push_back(r);
    }

    if (_scopeRepeaters.empty()) { scopeInfo(t("scope_no_repeaters")); return; }

    std::vector<String> opts;
    for (const auto& r : _scopeRepeaters) opts.push_back(r.name);
    _scopePickPhase = ScopePick::Repeater;
    openScopeRoller(t("scope_pick_repeater"), opts);
}

// Send the scope-list request and show a cancellable "requesting…" popup.
void SettingsScreen::beginScopeRequest(const uint8_t* pubKey, const String& name) {
    if (MeshManager::instance().isAnonReqPending()) { scopeInfo(t("scope_req_busy")); return; }
    uint32_t tag = 0, est = 0;
    if (!MeshManager::instance().requestScopeList(pubKey, tag, est)) {
        scopeInfo(t("scope_req_failed"));
        return;
    }
    // tick() enforces the timeout. Cap the wait so a lost reply doesn't hang the popup.
    uint32_t wait = est + 5000;                // reply grace
    if (wait > 20000) wait = 20000;            // hard ceiling
    _scopeReqExpiry = millis() + wait;
    _scopeReqDialog = ModalDialog::show(t("scope_from_repeater"),
        String(t("scope_requesting")) + "\n" + name, { String(t("btn_cancel")) },
        [this](lv_obj_t* dlg, int) {
            _scopeReqDialog = nullptr;   // stop waiting; a late reply is now ignored
            _scopeReqExpiry = 0;
            MeshManager::instance().clearAnonReq();   // free the radio for an immediate retry
            ModalDialog::close(dlg);
            returnToScopeEditor();
        });
}

// Called by UIManager when the mesh reply arrives. Guarded so stale/late replies
// (after cancel or timeout) are ignored.
void SettingsScreen::handleScopeListReply(const std::vector<String>& scopes) {
    if (!_scopeReqDialog) return;
    lv_obj_t* d = _scopeReqDialog;
    _scopeReqDialog = nullptr;
    _scopeReqExpiry = 0;
    ModalDialog::close(d);
    if (scopes.empty()) { scopeInfo(t("scope_none_named")); return; }
    _scopeNames = scopes;
    _scopePickPhase = ScopePick::Scope;
    openScopeRoller(t("scope_pick_scope"), scopes);
}

// Roller picker (repeater or scope, per _scopePickPhase). Mirrors the config choice
// picker: dim overlay (click to cancel), a roller, and an OK/Cancel row.
void SettingsScreen::openScopeRoller(const String& title, const std::vector<String>& options) {
    if (_scopeRollerPanel || options.empty()) return;

    _scopeRollerOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_scopeRollerOverlay, Display::width(), Display::height());
    lv_obj_set_pos(_scopeRollerOverlay, 0, 0);
    lv_obj_set_style_bg_color(_scopeRollerOverlay, theme::SCRIM(), 0);
    lv_obj_set_style_bg_opa(_scopeRollerOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_scopeRollerOverlay, 0, 0);
    lv_obj_clear_flag(_scopeRollerOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_scopeRollerOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_scopeRollerOverlay, [](lv_event_t* ev) {
        auto* s = (SettingsScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* p) { auto* self = (SettingsScreen*)p; self->hideScopeRoller(); self->returnToScopeEditor(); }, s);
    }, LV_EVENT_CLICKED, this);

    _scopeRollerPanel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_scopeRollerPanel, theme::MODAL_TEXT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_align(_scopeRollerPanel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_scopeRollerPanel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(_scopeRollerPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_scopeRollerPanel, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(_scopeRollerPanel, 1, 0);
    lv_obj_set_style_radius(_scopeRollerPanel, 8, 0);
    lv_obj_set_style_pad_all(_scopeRollerPanel, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_row(_scopeRollerPanel, theme::PAD_SMALL, 0);
    lv_obj_set_flex_flow(_scopeRollerPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(_scopeRollerPanel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLbl = lv_label_create(_scopeRollerPanel);
    lv_label_set_text(titleLbl, title.c_str());
    lv_obj_set_style_text_font(titleLbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(titleLbl, theme::TEXT_PRIMARY(), 0);

    String opts;
    for (size_t i = 0; i < options.size(); i++) { if (i) opts += "\n"; opts += options[i]; }

    _scopeRoller = lv_roller_create(_scopeRollerPanel);
    lv_roller_set_options(_scopeRoller, opts.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(_scopeRoller, 0, LV_ANIM_OFF);
    lv_obj_set_width(_scopeRoller, LV_PCT(100));
    lv_obj_set_style_text_font(_scopeRoller, FONT_HEADING, 0);
    lv_obj_set_style_text_color(_scopeRoller, theme::TEXT_PRIMARY(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_scopeRoller, theme::BG_SECONDARY(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_scopeRoller, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_scopeRoller, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_scopeRoller, theme::ACCENT(), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(_scopeRoller, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(_scopeRoller, theme::TEXT_ON_ACCENT(), LV_PART_SELECTED);
#ifdef PLATFORM_TWATCH
    lv_roller_set_visible_row_count(_scopeRoller, 3);
#else
    lv_roller_set_visible_row_count(_scopeRoller, 4);
#endif

    lv_obj_t* btnRow = lv_obj_create(_scopeRollerPanel);
    lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* okBtn = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(okBtn, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(okBtn, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(okBtn, scopeRollerOkCb, LV_EVENT_CLICKED, this);
    lv_obj_t* okLbl = lv_label_create(okBtn);
    lv_label_set_text(okLbl, t("btn_ok"));
    lv_obj_set_style_text_color(okLbl, theme::TEXT_ON_ACCENT(), 0);
    lv_obj_center(okLbl);

    lv_obj_t* cancelBtn = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(cancelBtn, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancelBtn, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancelBtn, [](lv_event_t* ev) {
        auto* s = (SettingsScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* p) { auto* self = (SettingsScreen*)p; self->hideScopeRoller(); self->returnToScopeEditor(); }, s);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* cxlLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    _scopeRollerGroup = g;
    lv_group_add_obj(g, _scopeRoller);
    lv_group_add_obj(g, okBtn);
    lv_group_add_obj(g, cancelBtn);
    lv_group_focus_obj(_scopeRoller);
    UIManager::instance().switchToModalGroup(_scopeRollerPanel);
    IInput::instance().attachToGroup(g);
}

void SettingsScreen::scopeRollerOkCb(lv_event_t* e) {
    auto* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_scopeRoller) return;
    uint16_t idx = lv_roller_get_selected(self->_scopeRoller);
    ScopePick phase = self->_scopePickPhase;

    if (phase == ScopePick::Repeater) {
        if (idx < self->_scopeRepeaters.size()) {
            ScopeRepeater r = self->_scopeRepeaters[idx];   // copy before teardown
            self->hideScopeRoller();
            self->beginScopeRequest(r.pubKey, r.name);
        } else {
            self->hideScopeRoller();
            self->returnToScopeEditor();
        }
    } else {  // Scope phase — chosen region fills the still-open editor textarea for review.
        if (idx < self->_scopeNames.size() && self->_scopeTextarea) {
            lv_textarea_set_text(self->_scopeTextarea, self->_scopeNames[idx].c_str());
        }
        self->hideScopeRoller();
        self->returnToScopeEditor();
    }
}

void SettingsScreen::hideScopeRoller() {
    if (!_scopeRollerPanel) return;
    UIManager::instance().restoreFromModalGroup();
    if (_scopeRollerGroup) { lv_group_del(_scopeRollerGroup); _scopeRollerGroup = nullptr; }
    _scopeRoller = nullptr;
    lv_obj_del_async(_scopeRollerPanel);   _scopeRollerPanel   = nullptr;
    lv_obj_del_async(_scopeRollerOverlay); _scopeRollerOverlay = nullptr;
}

// ─────────────────────────── Boot text editor ───────────────────────────

void SettingsScreen::bootTextReadyCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_bootTextTextarea) return;
    const char* text = lv_textarea_get_text(self->_bootTextTextarea);
    String newBootText = text ? String(text) : String("");
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
        g_dsDirty = true;
    }
    lv_async_call([](void* p) { ((SettingsScreen*)p)->hideBootTextEditor(); }, self);
}

void SettingsScreen::bootTextRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
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

    lv_obj_t* btnRow = lv_obj_create(self->_bootTextOverlay);
    lv_obj_set_size(btnRow, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btnRow, theme::PAD_SMALL, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_width(save, LV_PCT(100));
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, bootTextReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_width(cancel, LV_PCT(100));
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((SettingsScreen*)p)->hideBootTextEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    self->_editorGroup = g;
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
        auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_bootTextKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((SettingsScreen*)p)->hideBootTextEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

void SettingsScreen::hideBootTextEditor() {
    if (!_bootTextTextarea) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
#ifdef PLATFORM_TWATCH
    _bootTextKbd = nullptr;
#endif
    _bootTextTextarea = nullptr;
    lv_obj_del_async(_bootTextOverlay);
    _bootTextOverlay = nullptr;
    if (_screen) show();
}

// ─────────────────────────── Language picker ───────────────────────────

namespace {
std::vector<String>      g_langCodes;
std::vector<String>      g_langLabels;
std::vector<const char*> g_langMap;
}  // namespace

void SettingsScreen::hideLanguagePicker() {
    if (!_langBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    if (_editorGroup) { lv_group_del(_editorGroup); _editorGroup = nullptr; }
    lv_obj_del_async(_langBtnm);    _langBtnm = nullptr;
    lv_obj_del_async(_langOverlay); _langOverlay = nullptr;
    if (_screen) show();
}

void SettingsScreen::languageRowCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
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
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* ctx) { ((SettingsScreen*)ctx)->hideLanguagePicker(); }, s);
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
        SettingsScreen* s = (SettingsScreen*)lv_event_get_user_data(ev);
        uint16_t idx = lv_btnmatrix_get_selected_btn(s->_langBtnm);
        if (idx == LV_BTNMATRIX_BTN_NONE) return;
        if (idx < g_langCodes.size()) {
            auto& mgr = ConfigManager::instance();
            String newLang = g_langCodes[idx] == "en" ? String("") : g_langCodes[idx];
            if (mgr.config().language != newLang) {
                mgr.config().language = newLang;
                g_dsDirty = true;
                g_dsReboot = true;
                UIManager::instance().showToast(t("theme_apply_body"));
            }
        }
        lv_async_call([](void* ctx) { ((SettingsScreen*)ctx)->hideLanguagePicker(); }, s);
    }, LV_EVENT_CLICKED, self);

    UIManager::instance().switchToModalGroup(self->_langBtnm);
}

// ─────────────────────────── Inline sliders ───────────────────────────

void SettingsScreen::inlineSliderChangedCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);

    // Snap to the nearest 5 (clamped to each slider's min) so values land clean.
    auto snap5 = [](int32_t x, int32_t lo) { int32_t s = ((x + 2) / 5) * 5; return s < lo ? lo : s; };

    if (slider == self->_brightnessSlider) {
        v = snap5(v, 10);
        lv_slider_set_value(slider, v, LV_ANIM_OFF);
        lv_label_set_text(self->_brightnessValLbl, String(v).c_str());
        Display::instance().setBrightness((uint8_t)v);
    } else if (slider == self->_autoDimSlider) {
        v = snap5(v, 0);
        lv_slider_set_value(slider, v, LV_ANIM_OFF);
        String txt = v > 0 ? (String(v) + "s") : String(t("off"));
        lv_label_set_text(self->_autoDimValLbl, txt.c_str());
    } else if (slider == self->_dimBrightnessSlider) {
        v = snap5(v, 0);
        lv_slider_set_value(slider, v, LV_ANIM_OFF);
        String txt = v > 0 ? String(v) : String(t("off"));
        lv_label_set_text(self->_dimBrightnessValLbl, txt.c_str());
    } else if (slider == self->_kbdBrightnessSlider) {
        v = snap5(v, 5);
        lv_slider_set_value(slider, v, LV_ANIM_OFF);
        lv_label_set_text(self->_kbdBrightnessValLbl, String(v).c_str());
        IInput::instance().setBacklight((uint8_t)v);
    } else if (slider == self->_sosRepeatSlider) {
        lv_label_set_text(self->_sosRepeatValLbl, String(v).c_str());
    } else if (slider == self->_lowAlertSlider) {
        v = snap5(v, 5);
        lv_slider_set_value(slider, v, LV_ANIM_OFF);
        lv_label_set_text(self->_lowAlertValLbl, (String(v) + "%").c_str());
    } else if (slider == self->_txPowerSlider) {
        lv_label_set_text(self->_txPowerValLbl, (String(v) + " dBm").c_str());
    } else if (slider == self->_maxHistorySlider) {
        v = ((v + 5) / 10) * 10;
        lv_slider_set_value(slider, v, LV_ANIM_OFF);
        lv_label_set_text(self->_maxHistoryValLbl, String(v).c_str());
    } else if (slider == self->_maxRetriesSlider) {
        lv_label_set_text(self->_maxRetriesValLbl, String(v).c_str());
    } else if (slider == self->_clockOffsetSlider) {
        lv_label_set_text(self->_clockOffsetValLbl, (String(v) + "h").c_str());
    } else if (slider == self->_lastKnownSlider) {
        lv_label_set_text(self->_lastKnownValLbl, (String(v) + "m").c_str());
    }
}

void SettingsScreen::inlineSliderReleasedCb(lv_event_t* e) {
    SettingsScreen* self = (SettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);

    auto& mgr = ConfigManager::instance();
    if (slider == self->_brightnessSlider) {
        mgr.config().display.brightness = (uint8_t)v;
        g_dsDirty = true;
    } else if (slider == self->_autoDimSlider) {
        mgr.config().display.autoDimSeconds = (uint16_t)v;
        g_dsDirty = true;
    } else if (slider == self->_dimBrightnessSlider) {
        mgr.config().display.dimBrightness = (uint8_t)v;
        g_dsDirty = true;
    } else if (slider == self->_kbdBrightnessSlider) {
        mgr.config().display.kbdBrightness = (uint8_t)v;
        mgr.config().display.kbdBacklight = (v > 0);
        g_dsDirty = true;
    } else if (slider == self->_sosRepeatSlider) {
        mgr.config().sosRepeat = (uint8_t)v;
        g_dsDirty = true;
    } else if (slider == self->_lowAlertSlider) {
        mgr.config().battery.lowAlertThreshold = (uint8_t)v;
        g_dsDirty = true;
    } else if (slider == self->_txPowerSlider) {
        mgr.config().radio.txPower = (int8_t)v;
        g_dsDirty = true;
        g_dsReboot = true;
    } else if (slider == self->_maxHistorySlider) {
        mgr.config().messaging.maxHistoryPerChat = (uint16_t)v;
        g_dsDirty = true;
    } else if (slider == self->_maxRetriesSlider) {
        mgr.config().messaging.maxRetries = (uint8_t)v;
        g_dsDirty = true;
    } else if (slider == self->_clockOffsetSlider) {
        mgr.config().gpsClockOffset = (int8_t)v;
        g_dsDirty = true;
    } else if (slider == self->_lastKnownSlider) {
        mgr.config().gpsLastKnownMaxAge = (uint16_t)(v * 60);
        GPS::instance().setLastKnownMaxAge((uint16_t)(v * 60));  // apply live (no reboot)
        g_dsDirty = true;
    }
}

void SettingsScreen::emojiToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    bool newVal = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.config().display.emoji = newVal;
    g_dsDirty = true;
}

void SettingsScreen::screenshotsToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    bool newVal = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.config().debug.screenshots = newVal;
    g_dsDirty = true;
}

void SettingsScreen::showMemoryToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    mgr.config().debug.showMemory = lv_obj_has_state(sw, LV_STATE_CHECKED);
    g_dsDirty = true;
}

// No reboot needed — StatusBar::update() re-reads config every tick, so the
// gear appears/disappears on its own within a second.
void SettingsScreen::menuButtonToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    mgr.config().display.menuButton = lv_obj_has_state(sw, LV_STATE_CHECKED);
    g_dsDirty = true;
}

}  // namespace mclite
