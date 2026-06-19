#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace mclite {

class DeviceSettingsScreen {
public:
    void create(lv_obj_t* parent);
    void show();
    void hide();
    void tick();  // no-op for now, kept for symmetry

    lv_obj_t* obj() { return _screen; }

private:
    lv_obj_t* _screen   = nullptr;
    lv_obj_t* _content  = nullptr;
    lv_obj_t* _backBtn  = nullptr;

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
