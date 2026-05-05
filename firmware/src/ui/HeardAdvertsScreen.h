#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace mclite {

class HeardAdvertsScreen {
public:
    void create(lv_obj_t* parent);
    void show();
    void hide();
    void tick();  // call from UIManager::update(); rebuilds when cache version changes

    lv_obj_t* obj() { return _screen; }

private:
    lv_obj_t* _screen     = nullptr;
    lv_obj_t* _list       = nullptr;
    lv_obj_t* _emptyHint  = nullptr;
    lv_obj_t* _closeBtn   = nullptr;
    lv_obj_t* _clearBtn   = nullptr;

    // Detail modal state — only one open at a time
    lv_obj_t* _detailMsgbox = nullptr;
    String    _detailText;

    // Live-update bookkeeping
    uint32_t _lastVersion   = 0;
    uint32_t _lastRebuildMs = 0;

    void rebuild();
    void openDetail(int slotIdx);
    void closeDetail();

    static void closeBtnCb(lv_event_t* e);
    static void clearBtnCb(lv_event_t* e);
    static void rowClickCb(lv_event_t* e);
    static void detailBtnCb(lv_event_t* e);
};

}  // namespace mclite
