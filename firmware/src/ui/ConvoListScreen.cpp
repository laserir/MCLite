#include "ConvoListScreen.h"
#include "theme.h"
#include "../mesh/ContactStore.h"
#include "../mesh/ChannelStore.h"
#include "../hal/GPS.h"
#include "../i18n/I18n.h"
#include "../storage/TelemetryCache.h"
#include "../config/ConfigManager.h"

namespace mclite {

void ConvoListScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(parent);
    lv_obj_set_size(_screen, 320, 240 - theme::STATUS_BAR_HEIGHT);
    lv_obj_align(_screen, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_screen, theme::BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_set_style_radius(_screen, 0, 0);
    lv_obj_set_style_pad_all(_screen, 0, 0);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Scrollable list container
    _list = lv_obj_create(_screen);
    lv_obj_set_size(_list, 320, 240 - theme::STATUS_BAR_HEIGHT);
    lv_obj_align(_list, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_pad_all(_list, 0, 0);
    lv_obj_set_style_pad_row(_list, 1, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);

    // Scrollbar styling — thin, semi-transparent, right edge
    lv_obj_set_style_width(_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(_list, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(_list, theme::TEXT_SECONDARY, LV_PART_SCROLLBAR);

    // Empty state hint
    _emptyHint = lv_label_create(_screen);
    lv_obj_set_style_text_font(_emptyHint, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(_emptyHint, theme::TEXT_SECONDARY, 0);
    lv_label_set_text(_emptyHint, t("no_contacts"));
    lv_obj_set_style_text_align(_emptyHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_emptyHint, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(_emptyHint, LV_OBJ_FLAG_HIDDEN);
}

void ConvoListScreen::refresh() {
    // Remember which conversation had focus so we can restore it
    ConvoId focusedId{ConvoId::DM, ""};
    bool hadFocus = false;
    lv_group_t* grp = lv_group_get_default();
    if (grp) {
        lv_obj_t* focused = lv_group_get_focused(grp);
        if (focused) {
            ConvoId* id = (ConvoId*)lv_obj_get_user_data(focused);
            if (id) {
                focusedId = *id;
                hadFocus = true;
            }
        }
    }

    // Clear existing rows — remove from group first, lv_obj_clean doesn't
    if (grp) {
        uint32_t cnt = lv_obj_get_child_cnt(_list);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t* child = lv_obj_get_child(_list, i);
            lv_group_remove_obj(child);
        }
    }
    lv_obj_clean(_list);

    // Conversations are created at boot in main.cpp — just get sorted list
    auto convos = MessageStore::instance().getConversationsSorted();

    if (convos.empty()) {
        lv_obj_clear_flag(_emptyHint, LV_OBJ_FLAG_HIDDEN);
        return;
    } else {
        lv_obj_add_flag(_emptyHint, LV_OBJ_FLAG_HIDDEN);
    }

    for (auto* convo : convos) {
        addConvoRow(convo);
    }

    // Restore focus to the same conversation if it was focused before refresh
    if (grp && hadFocus) {
        uint32_t count = lv_obj_get_child_cnt(_list);
        for (uint32_t i = 0; i < count; i++) {
            lv_obj_t* child = lv_obj_get_child(_list, i);
            ConvoId* id = (ConvoId*)lv_obj_get_user_data(child);
            if (id && *id == focusedId) {
                lv_group_focus_obj(child);
                break;
            }
        }
    }
}

void ConvoListScreen::addConvoRow(Conversation* convo) {
    lv_obj_t* row = lv_obj_create(_list);
    lv_obj_set_size(row, 316, theme::CONVO_ROW_HEIGHT);
    lv_obj_set_style_bg_color(row, theme::BG_SECONDARY, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_row(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);

    // Highlight on focus
    lv_obj_set_style_bg_color(row, theme::ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    // Add to default input group for trackball/encoder navigation
    lv_group_t* grp = lv_group_get_default();
    if (grp) lv_group_add_obj(grp, row);

    // Auto-scroll list when trackball moves focus to this row
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
        lv_obj_scroll_to_view(lv_event_get_target(e), LV_ANIM_ON);
    }, LV_EVENT_FOCUSED, nullptr);

    // Store conversation ID in user data (freed on LV_EVENT_DELETE)
    ConvoId* idCopy = new ConvoId(convo->convoId);
    lv_obj_set_user_data(row, idCopy);
    lv_obj_add_event_cb(row, rowClickCb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
        ConvoId* id = (ConvoId*)lv_obj_get_user_data(lv_event_get_target(e));
        delete id;
    }, LV_EVENT_DELETE, nullptr);

    // Top line: icon + name + timestamp
    lv_obj_t* topLine = lv_obj_create(row);
    lv_obj_set_size(topLine, 300, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(topLine, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topLine, 0, 0);
    lv_obj_set_style_pad_all(topLine, 0, 0);
    lv_obj_clear_flag(topLine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(topLine, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topLine, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Type icon
    lv_obj_t* icon = lv_label_create(topLine);
    lv_obj_set_style_text_font(icon, FONT_NORMAL, 0);
    if (convo->convoId.type == ConvoId::ROOM) {
        lv_label_set_text(icon, ICON_ROOM);
        lv_obj_set_style_text_color(icon, theme::ROOM_ACCENT, 0);
    } else if (convo->convoId.type == ConvoId::DM) {
        lv_label_set_text(icon, ICON_DM);
        lv_obj_set_style_text_color(icon, theme::ACCENT, 0);
    } else if (convo->isPrivate) {
        lv_label_set_text(icon, ICON_PRIVATE);
        lv_obj_set_style_text_color(icon, theme::TEXT_SECONDARY, 0);
    } else {
        lv_label_set_text(icon, ICON_CHANNEL);
        lv_obj_set_style_text_color(icon, theme::TEXT_SECONDARY, 0);
    }

    // Unread dot
    if (convo->hasUnread) {
        lv_obj_t* dot = lv_label_create(topLine);
        lv_obj_set_style_text_font(dot, FONT_NORMAL, 0);
        lv_obj_set_style_text_color(dot, theme::UNREAD_DOT, 0);
        lv_label_set_text(dot, " " LV_SYMBOL_BULLET);
    }

    // Name
    lv_obj_t* name = lv_label_create(topLine);
    lv_obj_set_style_text_font(name, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(name, theme::TEXT_PRIMARY, 0);
    lv_obj_set_flex_grow(name, 1);
    String nameStr = " " + convo->displayName;
    lv_label_set_text(name, nameStr.c_str());
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    // Last-seen + telemetry badges for DM conversations
    if (convo->convoId.type == ConvoId::DM) {
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const Contact* c = contacts.findByIndex(i);
            if (!c || c->shortId() != convo->convoId.id) continue;

            // Online indicator + last-seen time
            if (c->lastSeen > 0) {
                uint32_t age = millis() - c->lastSeen;

                // Eye icon if < 20min (tolerates one missed advert cycle)
                if (age < 1200000) {
                    lv_obj_t* seenIcon = lv_label_create(topLine);
                    lv_obj_set_style_text_font(seenIcon, FONT_SMALL, 0);
                    lv_obj_set_style_text_color(seenIcon, theme::ONLINE_DOT, 0);
                    lv_label_set_text(seenIcon, LV_SYMBOL_EYE_OPEN);
                }

                // Time label
                String seenText = formatLastSeen(c->lastSeen);
                if (seenText.length() > 0) {
                    lv_obj_t* seenLabel = lv_label_create(topLine);
                    lv_obj_set_style_text_font(seenLabel, FONT_SMALL, 0);
                    lv_obj_set_style_text_color(seenLabel, theme::TEXT_TIMESTAMP, 0);
                    lv_label_set_text(seenLabel, seenText.c_str());
                }
            }

            // Telemetry badges — independent of lastSeen
            const auto& showTelem = ConfigManager::instance().config().messaging.showTelemetry;
            if (showTelem != "none") {
                const TelemetryData* td = TelemetryCache::instance().get(c->publicKey);
                if (td && TelemetryCache::instance().isFresh(c->publicKey)) {
                    // Battery icon (tiered, same as own battery in status bar)
                    if (td->hasVoltage && (showTelem == "battery" || showTelem == "both")) {
                        int pct = constrain((int)((td->voltage - 3.0f) / 1.2f * 100.0f), 0, 100);
                        const char* battSym;
                        if (pct > 80)      battSym = LV_SYMBOL_BATTERY_FULL;
                        else if (pct > 60) battSym = LV_SYMBOL_BATTERY_3;
                        else if (pct > 40) battSym = LV_SYMBOL_BATTERY_2;
                        else if (pct > 20) battSym = LV_SYMBOL_BATTERY_1;
                        else               battSym = LV_SYMBOL_BATTERY_EMPTY;
                        lv_obj_t* battIcon = lv_label_create(topLine);
                        lv_obj_set_style_text_font(battIcon, FONT_SMALL, 0);
                        lv_label_set_text(battIcon, battSym);
                        lv_obj_set_style_text_color(battIcon,
                            pct <= 20 ? theme::BATTERY_LOW : theme::TEXT_PRIMARY, 0);
                    }
                    // GPS icon
                    if (td->hasLocation && (showTelem == "location" || showTelem == "both")) {
                        lv_obj_t* locIcon = lv_label_create(topLine);
                        lv_obj_set_style_text_font(locIcon, FONT_SMALL, 0);
                        lv_label_set_text(locIcon, LV_SYMBOL_GPS);
                        lv_obj_set_style_text_color(locIcon, theme::TEXT_PRIMARY, 0);
                    }
                }
            }

            break;
        }
    }

    // Timestamp — show for channels and rooms (DMs already have last-seen above)
    const Message* lastMsg = convo->lastMessage();
    if ((convo->convoId.type == ConvoId::CHANNEL ||
         convo->convoId.type == ConvoId::ROOM) && lastMsg &&
        lastMsg->timestamp > 1700000000 && GPS::instance().isTimeSynced()) {
        // Use last message's Unix epoch timestamp for relative time display
        uint32_t now = GPS::instance().currentTimestamp();
        uint32_t diff = (now > lastMsg->timestamp) ? (now - lastMsg->timestamp) : 0;
        char timeBuf[32];
        if (diff < 60)            snprintf(timeBuf, sizeof(timeBuf), t("time_s"), (int)diff);
        else if (diff < 3600)     snprintf(timeBuf, sizeof(timeBuf), t("time_m"), (int)(diff / 60));
        else if (diff < 86400)    snprintf(timeBuf, sizeof(timeBuf), t("time_h"), (int)(diff / 3600));
        else                      snprintf(timeBuf, sizeof(timeBuf), t("time_d"), (int)(diff / 86400));
        String timeStr = timeBuf;
        lv_obj_t* ts = lv_label_create(topLine);
        lv_obj_set_style_text_font(ts, FONT_SMALL, 0);
        lv_obj_set_style_text_color(ts, theme::TEXT_TIMESTAMP, 0);
        lv_label_set_text(ts, timeStr.c_str());
    }

    // Bottom line: last message preview
    const Message* last = convo->lastMessage();
    if (last && last->text.length() > 0) {
        lv_obj_t* preview = lv_label_create(row);
        lv_obj_set_style_text_font(preview, FONT_SMALL, 0);
        lv_obj_set_style_text_color(preview, theme::TEXT_SECONDARY, 0);
        lv_obj_set_width(preview, 300);
        lv_label_set_long_mode(preview, LV_LABEL_LONG_DOT);

        String previewText = last->text;
        if (previewText.length() > 40) {
            previewText = previewText.substring(0, 40);
        }
        lv_label_set_text(preview, previewText.c_str());
    }
}

void ConvoListScreen::rowClickCb(lv_event_t* e) {
    ConvoListScreen* self = (ConvoListScreen*)lv_event_get_user_data(e);
    lv_obj_t* row = lv_event_get_current_target(e);  // The row, not a child label
    ConvoId* id = (ConvoId*)lv_obj_get_user_data(row);
    if (id && self->_onSelect) {
        self->_onSelect(*id);
    }
}

void ConvoListScreen::show() {
    if (_screen) lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    refresh();
}

void ConvoListScreen::hide() {
    if (!_screen) return;

    // Remove rows from input group before hiding (prevents trackball
    // navigating to invisible items when another screen is active)
    lv_group_t* grp = lv_group_get_default();
    if (grp && _list) {
        uint32_t cnt = lv_obj_get_child_cnt(_list);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_group_remove_obj(lv_obj_get_child(_list, i));
        }
    }

    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}


String ConvoListScreen::formatLastSeen(uint32_t lastSeenMs) {
    if (lastSeenMs == 0) return "";
    uint32_t diff = (millis() - lastSeenMs) / 1000;  // unsigned wrap is fine

    char buf[32];
    if (diff < 60)       { snprintf(buf, sizeof(buf), t("time_s"), (int)diff); return buf; }
    if (diff < 3600)     { snprintf(buf, sizeof(buf), t("time_m"), (int)(diff / 60)); return buf; }
    if (diff < 86400)    { snprintf(buf, sizeof(buf), t("time_h"), (int)(diff / 3600)); return buf; }
    snprintf(buf, sizeof(buf), t("time_d"), (int)(diff / 86400)); return buf;
}

}  // namespace mclite
