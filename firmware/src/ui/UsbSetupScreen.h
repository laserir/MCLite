#pragma once

#include <lvgl.h>

namespace mclite {

// Minimal dedicated screen for USB companion mode: a single switch + live status.
// Bridges the radio to a computer over the USB-CDC port (mutually exclusive with
// WiFi companion; mutes serial debug logging while active). Reached from the
// admin screen.
class UsbSetupScreen {
public:
    void create(lv_obj_t* parent);
    void show();
    void hide();
    void tick();   // live-refresh when a client attaches/detaches
    lv_obj_t* obj() { return _screen; }

private:
    lv_obj_t* _screen      = nullptr;
    lv_obj_t* _switch      = nullptr;
    lv_obj_t* _statusLabel = nullptr;
    lv_obj_t* _backBtn     = nullptr;
    bool _lastClient = false;

    void updateUi();
    static void switchCb(lv_event_t* e);
    static void backBtnCb(lv_event_t* e);
};

}  // namespace mclite
