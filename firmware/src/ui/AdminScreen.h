#pragma once

#include <lvgl.h>

namespace mclite {

// AdminScreen — placeholder for future PIN-protected settings
// v1: All configuration via SD card only
class AdminScreen {
public:
    void create(lv_obj_t* parent);
    void show();
    void hide();

    lv_obj_t* obj() { return _screen; }

private:
    lv_obj_t* _screen = nullptr;
    lv_obj_t* _closeBtn = nullptr;
    static void closeBtnCb(lv_event_t* e);
    static void offgridToggleCb(lv_event_t* e);
};

}  // namespace mclite
