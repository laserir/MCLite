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
    void tick();  // refresh dynamic counts (heard-adverts) while screen is visible

    lv_obj_t* obj() { return _screen; }

private:
    lv_obj_t* _screen   = nullptr;
    lv_obj_t* _closeBtn = nullptr;

    // Dynamic-count row state. Reset every show() since the row is rebuilt.
    lv_obj_t* _heardCountLabel = nullptr;
    uint32_t  _heardCacheVersion = 0;

    static void closeBtnCb(lv_event_t* e);
    static void offgridToggleCb(lv_event_t* e);
};

}  // namespace mclite
