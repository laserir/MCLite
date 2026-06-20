#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace mclite {

class MessagingSettingsScreen {
public:
    void create(lv_obj_t* parent);
    void show();
    void hide();
    void tick();

    lv_obj_t* obj() { return _screen; }

private:
    lv_obj_t* _screen   = nullptr;
    lv_obj_t* _content  = nullptr;
    lv_obj_t* _backBtn  = nullptr;

    // History editor
    lv_obj_t* _historyOverlay      = nullptr;
    lv_obj_t* _historyTextarea     = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _historyKbd          = nullptr;
#endif

    // Retries slider
    lv_obj_t* _maxRetriesSlider    = nullptr;
    lv_obj_t* _maxRetriesValLbl    = nullptr;

    // Location format picker
    lv_obj_t* _locFormatBtnm       = nullptr;
    void hideLocFormatPicker();
    static void locFormatRowCb(lv_event_t* e);
    static void locFormatChosenCb(lv_event_t* e);

    // Telemetry show picker
    lv_obj_t* _showTelemBtnm       = nullptr;
    void hideShowTelemPicker();
    static void showTelemRowCb(lv_event_t* e);
    static void showTelemChosenCb(lv_event_t* e);

    // Canned custom messages editor
    lv_obj_t* _cannedOverlay       = nullptr;
    lv_obj_t* _cannedTextarea      = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _cannedKbd           = nullptr;
#endif
    int        _cannedEditIndex    = -1;   // which of the 8 slots we're editing
    void hideCannedEditor();
    static void cannedCustomRowCb(lv_event_t* e);
    static void cannedReadyCb(lv_event_t* e);

    // History editor callbacks
    void hideHistoryEditor();
    static void historyRowCb(lv_event_t* e);
    static void historyReadyCb(lv_event_t* e);

    // Helper
    lv_obj_t* createRowContainer(lv_obj_t* parent);

    // Toggle callbacks
    static void saveHistoryToggleCb(lv_event_t* e);
    static void requestTelemetryToggleCb(lv_event_t* e);
    static void cannedMessagesToggleCb(lv_event_t* e);
    static void allowMuteToggleCb(lv_event_t* e);
    static void autoTelemetryToggleCb(lv_event_t* e);

    // Inline slider callbacks
    static void inlineSliderChangedCb(lv_event_t* e);
    static void inlineSliderReleasedCb(lv_event_t* e);

    static void backBtnCb(lv_event_t* e);
};

}  // namespace mclite
