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
    lv_obj_t* _lblOffgrid = nullptr;
    lv_obj_t* _lblName    = nullptr;
    lv_obj_t* _soundIcon  = nullptr;
    lv_obj_t* _lblBatt    = nullptr;
    lv_obj_t* _lblTime    = nullptr;
    lv_obj_t* _gpsIcon    = nullptr;

    static void soundClickCb(lv_event_t* e);
};

}  // namespace mclite
