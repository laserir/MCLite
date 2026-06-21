#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace mclite {

// One reusable settings screen rendered per config-tool section. Conversations
// (Contacts/Channels/Rooms) render read-only for now.
// CamelCase members on purpose — ALL-CAPS names (DEVICE, GPS, …) collide with
// Arduino/board macros.
enum class SettingsSection {
    Device, Radio, Display, Messaging, Sound, Gps, Battery, Security,
    Contacts, Channels, Rooms
};

// Generic enum/string choice fields rendered by the shared btnmatrix picker.
enum class ChoiceField { LocationFormat, ShowTelemetry, LocationPrecision, RegionPreset, AdvertInterval };

// Simple bool config fields toggled by the shared lv_switch callback. The id is
// stashed in the switch's user_data so one callback maps to the right field.
enum class BoolField {
    SaveHistory = 1, RequestTelemetry, AutoTelemetry, CannedMessages, AllowMute, GpsEnabled
};

class SettingsScreen {
public:
    void create(lv_obj_t* parent);
    void setSection(SettingsSection s) { _section = s; }
    void show();
    void hide();
    void tick();  // refreshes the live Heard-Adverts count on the Radio section

    lv_obj_t* obj() { return _screen; }

private:
    SettingsSection _section = SettingsSection::Device;
    lv_obj_t* _screen     = nullptr;
    lv_obj_t* _content    = nullptr;
    lv_obj_t* _backBtn    = nullptr;
    lv_obj_t* _titleLabel = nullptr;  // window title, updated per section in show()

    // Local input group for the open text editor (textarea + Save/Cancel). Owned
    // here so it's freed on close — the editors create their own group separate
    // from UIManager's modal group, which only manages the overlay.
    lv_group_t* _editorGroup = nullptr;

    // Theme picker overlay (canned-message-style btnmatrix).
    lv_obj_t* _themeOverlay = nullptr;
    lv_obj_t* _themeBtnm    = nullptr;
    void hideThemePicker();

    // Device name editor overlay
    lv_obj_t* _nameOverlay  = nullptr;
    lv_obj_t* _nameTextarea = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _nameKbd      = nullptr;
#endif
    void hideNameEditor();
    static void nameRowCb(lv_event_t* e);
    static void nameReadyCb(lv_event_t* e);

    // Boot text editor overlay
    lv_obj_t* _bootTextOverlay  = nullptr;
    lv_obj_t* _bootTextTextarea = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _bootTextKbd      = nullptr;
#endif
    void hideBootTextEditor();
    static void bootTextRowCb(lv_event_t* e);
    static void bootTextReadyCb(lv_event_t* e);

    // Language picker overlay
    lv_obj_t* _langOverlay = nullptr;
    lv_obj_t* _langBtnm    = nullptr;
    void hideLanguagePicker();
    static void languageRowCb(lv_event_t* e);

    // Security pickers/editor
    lv_obj_t* _lockModeBtnm = nullptr;
    lv_obj_t* _autoLockBtnm = nullptr;
    lv_obj_t* _pinOverlay   = nullptr;
    lv_obj_t* _pinTextarea = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _pinKbd      = nullptr;
#endif
    void hideLockModePicker();
    void hideAutoLockPicker();
    void hidePinEditor();
    static void lockModeRowCb(lv_event_t* e);
    static void lockModeChosenCb(lv_event_t* e);
    static void pinRowCb(lv_event_t* e);
    static void pinReadyCb(lv_event_t* e);
    static void autoLockRowCb(lv_event_t* e);
    static void autoLockChosenCb(lv_event_t* e);

    // Sound settings
    lv_obj_t* _sosKeywordOverlay  = nullptr;
    lv_obj_t* _sosKeywordTextarea = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _sosKeywordKbd      = nullptr;
#endif
    void hideSosKeywordEditor();
    static void sosKeywordRowCb(lv_event_t* e);
    static void sosKeywordReadyCb(lv_event_t* e);

    lv_obj_t* _sosRepeatSlider    = nullptr;
    lv_obj_t* _sosRepeatValLbl    = nullptr;
    lv_obj_t* _lowAlertSlider     = nullptr;
    lv_obj_t* _lowAlertValLbl     = nullptr;
    static void soundToggleCb(lv_event_t* e);
    static void lowAlertToggleCb(lv_event_t* e);

    // Radio section TX-power slider — reboots on leave. (Advert interval is a
    // picker, see ChoiceField::AdvertInterval.)
    lv_obj_t* _txPowerSlider   = nullptr;
    lv_obj_t* _txPowerValLbl   = nullptr;

    // Messaging section sliders.
    lv_obj_t* _maxHistorySlider = nullptr;
    lv_obj_t* _maxHistoryValLbl = nullptr;
    lv_obj_t* _maxRetriesSlider = nullptr;
    lv_obj_t* _maxRetriesValLbl = nullptr;

    // GPS section sliders.
    lv_obj_t* _clockOffsetSlider = nullptr;
    lv_obj_t* _clockOffsetValLbl = nullptr;
    lv_obj_t* _lastKnownSlider   = nullptr;
    lv_obj_t* _lastKnownValLbl   = nullptr;

    // Live Heard-Adverts count row (Radio section) — refreshed by tick().
    lv_obj_t* _heardCountLabel = nullptr;
    uint32_t  _heardCacheVersion = 0;
    static void heardAdvertsRowCb(lv_event_t* e);

    // Offgrid toggle (Radio section) — opens a reboot-confirm dialog.
    static void offgridRowCb(lv_event_t* e);

    // Generic bool switch + the shared toggle callback (BoolField via user_data).
    static void boolToggleCb(lv_event_t* e);
    static void gpsToggleCb(lv_event_t* e);  // GPS enable needs reboot to (de)init

    // Shared enum/string choice picker (roller). _choiceField selects the set.
    ChoiceField _choiceField   = ChoiceField::LocationFormat;
    lv_obj_t*   _choicePanel   = nullptr;  // modal container (overlay child)
    lv_obj_t*   _choiceRoller  = nullptr;
    lv_obj_t*   _choiceOverlay = nullptr;
    void openChoicePicker(ChoiceField f);
    void hideChoicePicker();
    static void choiceChosenCb(lv_event_t* e);
    static void locFormatRowCb(lv_event_t* e);
    static void showTelemetryRowCb(lv_event_t* e);
    static void locPrecisionRowCb(lv_event_t* e);
    static void regionRowCb(lv_event_t* e);
    static void advertRowCb(lv_event_t* e);

    // Timezone text editor (GPS section).
    lv_obj_t* _timezoneOverlay  = nullptr;
    lv_obj_t* _timezoneTextarea = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _timezoneKbd      = nullptr;
#endif
    void hideTimezoneEditor();
    static void timezoneRowCb(lv_event_t* e);
    static void timezoneReadyCb(lv_event_t* e);

    // Per-section body builders (keep show() readable). Each appends to _content.
    void buildDevice();
    void buildRadio();
    void buildDisplay();
    void buildMessaging();
    void buildSound();
    void buildGps();
    void buildBattery();
    void buildSecurity();
    void buildConvoList();  // Contacts / Channels / Rooms read-only views

    // Small row helpers used across sections (cut the boilerplate).
    lv_obj_t* addReadOnlyRow(const char* label, const String& value);
    lv_obj_t* addNavRow(const char* label, const String& value, lv_event_cb_t cb);
    void      addSwitchRow(const char* label, bool checked, lv_event_cb_t cb,
                           void* swUserData, bool rowClickable = true);
    lv_obj_t* addSliderRow(const char* label, lv_obj_t** sliderOut, lv_obj_t** valLblOut,
                           int32_t lo, int32_t hi, int32_t val, const String& valText);
    void addSectionHeader(const char* title);

    // Permission gate (permissions.settings): is a control editable now? `basic`
    // controls (brightness family, theme) stay editable in "restricted" mode;
    // everything else needs "full". "none" locks everything. The *-Gated helpers
    // render the interactive control when allowed, else a read-only value row.
    bool canEdit(bool basic) const;
    void addNavRowGated(const char* label, const String& value, lv_event_cb_t cb, bool basic);
    void addSwitchRowGated(const char* label, bool checked, lv_event_cb_t cb, void* ud, bool basic);
    void addSliderRowGated(const char* label, lv_obj_t** sliderOut, lv_obj_t** valLblOut,
                           int32_t lo, int32_t hi, int32_t val, const String& valText, bool basic);

    // Inline sliders for brightness / auto-dim / dim brightness / keyboard brightness
    lv_obj_t* _brightnessSlider     = nullptr;
    lv_obj_t* _brightnessValLbl     = nullptr;
    lv_obj_t* _autoDimSlider        = nullptr;
    lv_obj_t* _autoDimValLbl        = nullptr;
    lv_obj_t* _dimBrightnessSlider  = nullptr;
    lv_obj_t* _dimBrightnessValLbl  = nullptr;
    lv_obj_t* _kbdBrightnessSlider  = nullptr;
    lv_obj_t* _kbdBrightnessValLbl  = nullptr;
    static void inlineSliderChangedCb(lv_event_t* e);
    static void inlineSliderReleasedCb(lv_event_t* e);

    // Small helper to create a styled row container
    lv_obj_t* createRowContainer(lv_obj_t* parent);

    // Pending PIN during two-step confirmation
    String _pendingPin;

    static void backBtnCb(lv_event_t* e);
    static void themeRowCb(lv_event_t* e);   // opens the theme picker (reboots to apply)
    static void emojiToggleCb(lv_event_t* e);
    static void screenshotsToggleCb(lv_event_t* e);
};

}  // namespace mclite
