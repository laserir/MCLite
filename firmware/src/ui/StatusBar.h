#pragma once

#include <lvgl.h>

namespace mclite {

class StatusBar {
public:
    void create(lv_obj_t* parent);
    void update();  // Refresh battery, time, GPS indicator

    // Update sound icon to reflect mute state
    void updateSoundIcon();

    lv_obj_t* obj() { return _bar; }

private:
    lv_obj_t* _bar        = nullptr;
    lv_obj_t* _nameRow    = nullptr;  // T-Watch only: row 1 container
    lv_obj_t* _iconRow    = nullptr;  // T-Watch only: row 2 container
    lv_obj_t* _footer     = nullptr;  // T-Watch only: bottom bar with clock
    lv_obj_t* _lblOffgrid = nullptr;
    lv_obj_t* _lblName    = nullptr;
    lv_obj_t* _soundIcon  = nullptr;
    lv_obj_t* _lblBatt    = nullptr;
    lv_obj_t* _lblTime    = nullptr;
    lv_obj_t* _gpsIcon    = nullptr;
    lv_obj_t* _wifiIcon   = nullptr;  // shown only while WiFi is connected
    lv_obj_t* _bleIcon    = nullptr;  // shown only while BLE companion is active
    lv_obj_t* _lblMem     = nullptr;  // "P{X}% R{Y}%" PSRAM/RAM usage
    lv_obj_t* _menuBtn    = nullptr;  // T-Deck only: leftmost gear -> Admin (display.menu_button)

    static void soundClickCb(lv_event_t* e);
    static void gpsClickCb(lv_event_t* e);
    static void menuClickCb(lv_event_t* e);
};

}  // namespace mclite
