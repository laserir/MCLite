#pragma once

#include <lvgl.h>
#include <functional>
#include "../storage/MessageStore.h"

namespace mclite {

using OnConvoSelectCallback = std::function<void(const ConvoId& id)>;
using OnConvoMuteCallback   = std::function<void(const ConvoId& id, bool muted)>;

class ConvoListScreen {
public:
    void create(lv_obj_t* parent);
    void refresh();  // Rebuild list from MessageStore
    void show();
    void hide();

    void onSelect(OnConvoSelectCallback cb) { _onSelect = cb; }
    void onMute(OnConvoMuteCallback cb)   { _onMute = cb; }

    lv_obj_t* obj() { return _screen; }

private:
    lv_obj_t* _screen    = nullptr;
    lv_obj_t* _list      = nullptr;
    lv_obj_t* _emptyHint = nullptr;

    OnConvoSelectCallback _onSelect;
    OnConvoMuteCallback   _onMute;

    void addConvoRow(Conversation* convo);
    static void rowClickCb(lv_event_t* e);
    static void rowLongPressCb(lv_event_t* e);

    // Format last-seen age from millis() timestamp
    static String formatLastSeen(uint32_t lastSeenMs);
};

}  // namespace mclite
