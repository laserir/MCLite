#include "HeardAdvertsScreen.h"
#include "UIManager.h"
#include "theme.h"
#include "../config/ConfigManager.h"
#include "../config/defaults.h"
#include "../i18n/I18n.h"
#include "../mesh/ContactStore.h"
#include "../storage/HeardAdvertCache.h"
#include "../util/hex.h"

#include <helpers/AdvertDataHelpers.h>  // ADV_TYPE_*
#include <algorithm>

namespace mclite {

namespace {

const char* typeIcon(uint8_t type) {
    switch (type) {
        case ADV_TYPE_CHAT:     return "@";
        case ADV_TYPE_REPEATER: return "P";
        case ADV_TYPE_ROOM:     return "R";
        case ADV_TYPE_SENSOR:   return "S";
        default:                return "?";
    }
}

lv_color_t typeColor(uint8_t type) {
    switch (type) {
        case ADV_TYPE_CHAT:     return theme::ACCENT;
        case ADV_TYPE_REPEATER: return theme::TEXT_PRIMARY;
        case ADV_TYPE_ROOM:     return theme::ROOM_ACCENT;
        case ADV_TYPE_SENSOR:   return theme::OFFGRID_ACCENT;
        default:                return theme::TEXT_TIMESTAMP;
    }
}

const char* typeLabel(uint8_t type) {
    switch (type) {
        case ADV_TYPE_CHAT:     return t("heard_type_chat");
        case ADV_TYPE_REPEATER: return t("heard_type_repeater");
        case ADV_TYPE_ROOM:     return t("heard_type_room");
        case ADV_TYPE_SENSOR:   return t("heard_type_sensor");
        default:                return "?";
    }
}

String formatHops(uint8_t hops) {
    if (hops == 0) return t("heard_direct");
    if (hops == 1) return t("heard_one_hop");
    char buf[16];
    snprintf(buf, sizeof(buf), t("heard_hops_fmt"), (int)hops);
    return buf;
}

// Render the per-hop path as " > "-separated lowercase hex chunks.
// Each chunk is hashSize × 2 hex chars. Returns "" when no path.
//
// Direction: Mesh.cpp:332-334 has every forwarder append its own hash before
// retransmitting, so path[0] is the hop closest to the sender and the last
// chunk is the hop closest to us. The "sender > … > us" arrows here read
// left-to-right in that natural traversal order.
String formatPath(const uint8_t* bytes, uint8_t pathByteLen, uint8_t hashSize) {
    if (pathByteLen == 0 || hashSize == 0) return "";
    String out;
    char hex[3];
    int hopCount = pathByteLen / hashSize;
    for (int h = 0; h < hopCount; h++) {
        if (h > 0) out += " > ";
        for (int b = 0; b < hashSize; b++) {
            sprintf(hex, "%02x", bytes[h * hashSize + b]);
            out += hex;
        }
    }
    return out;
}

// Render the 32-byte pubkey as 4-byte (8-hex) groups, space-separated,
// 4 groups per line — fingerprint style. Two lines total. Fits the modal
// width without ambiguous wrapping.
String formatKeyChunked(const uint8_t* key) {
    String out;
    char hex[3];
    for (int i = 0; i < 32; i++) {
        sprintf(hex, "%02x", key[i]);
        out += hex;
        if (i == 31) break;
        if (i == 15) out += "\n";
        else if ((i + 1) % 4 == 0) out += " ";
    }
    return out;
}

String formatAge(uint32_t lastHeardMs) {
    uint32_t diffMs = millis() - lastHeardMs;
    uint32_t s = diffMs / 1000;
    char buf[24];
    if (s < 60)         { snprintf(buf, sizeof(buf), t("time_s"), (int)s);          return buf; }
    if (s < 3600)       { snprintf(buf, sizeof(buf), t("time_m"), (int)(s / 60));   return buf; }
    if (s < 86400)      { snprintf(buf, sizeof(buf), t("time_h"), (int)(s / 3600)); return buf; }
    snprintf(buf, sizeof(buf), t("time_d"), (int)(s / 86400));
    return buf;
}

}  // namespace

void HeardAdvertsScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(parent);
    lv_obj_set_size(_screen, 320, 240 - theme::STATUS_BAR_HEIGHT);
    lv_obj_align(_screen, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_screen, theme::BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_set_style_radius(_screen, 0, 0);
    lv_obj_set_style_pad_all(_screen, 0, 0);  // edge-to-edge; header carries its own inset
    lv_obj_set_style_pad_row(_screen, theme::PAD_SMALL, 0);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Header row: title + clear + close. Inset slightly so the buttons don't
    // touch the screen edges; list below stays full-width.
    lv_obj_t* header = lv_obj_create(_screen);
    lv_obj_set_size(header, 320, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_hor(header, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_ver(header, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_column(header, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title = lv_label_create(header);
    lv_obj_set_style_text_font(title, FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY, 0);
    lv_obj_set_flex_grow(title, 1);  // title fills, buttons hug right
    lv_label_set_text(title, t("heard_adverts_title"));

    auto makeIconBtn = [](lv_obj_t* parent, const char* sym,
                          lv_event_cb_t cb, void* user) {
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 32, 24);
        lv_obj_set_style_bg_color(btn, theme::BG_SECONDARY, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_bg_color(btn, theme::ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_FOCUSED);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, FONT_NORMAL, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY, 0);
        lv_label_set_text(lbl, sym);
        lv_obj_center(lbl);
        return btn;
    };

    _clearBtn = makeIconBtn(header, LV_SYMBOL_TRASH, clearBtnCb, this);
    _closeBtn = makeIconBtn(header, LV_SYMBOL_CLOSE, closeBtnCb, nullptr);

    // Scrollable list container — full screen width
    _list = lv_obj_create(_screen);
    lv_obj_set_size(_list, 320, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(_list, 1);
    lv_obj_set_style_bg_opa(_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_pad_all(_list, 0, 0);
    lv_obj_set_style_pad_row(_list, 1, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(_list, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(_list, theme::TEXT_SECONDARY, LV_PART_SCROLLBAR);

    // Empty hint
    _emptyHint = lv_label_create(_screen);
    lv_obj_set_style_text_font(_emptyHint, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(_emptyHint, theme::TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(_emptyHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_emptyHint, t("heard_adverts_empty"));
    lv_obj_add_flag(_emptyHint, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void HeardAdvertsScreen::show() {
    if (!_screen) return;
    rebuild();
    _lastVersion   = HeardAdvertCache::instance().version();
    _lastRebuildMs = millis();
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void HeardAdvertsScreen::hide() {
    if (!_screen) return;

    closeDetail();

    lv_group_t* grp = lv_group_get_default();
    if (grp && _list) {
        uint32_t cnt = lv_obj_get_child_cnt(_list);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_group_remove_obj(lv_obj_get_child(_list, i));
        }
        lv_group_remove_obj(_clearBtn);
        lv_group_remove_obj(_closeBtn);
    }

    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void HeardAdvertsScreen::rebuild() {
    auto& cache = HeardAdvertCache::instance();
    auto& contacts = ContactStore::instance();
    lv_group_t* grp = lv_group_get_default();

    // Snapshot focused row's pubkey so we can re-focus it after rebuild.
    uint8_t focusedKey[32] = {};
    bool    hadFocus = false;
    if (grp) {
        lv_obj_t* focused = lv_group_get_focused(grp);
        if (focused) {
            uint32_t cnt = lv_obj_get_child_cnt(_list);
            for (uint32_t i = 0; i < cnt; i++) {
                if (lv_obj_get_child(_list, i) == focused) {
                    int slot = (int)(intptr_t)lv_obj_get_user_data(focused);
                    if (slot >= 0 && slot < cache.count()) {
                        memcpy(focusedKey, cache.entries()[slot].pubKey, 32);
                        hadFocus = true;
                    }
                    break;
                }
            }
        }
    }

    // Drop existing rows + header buttons from group
    if (grp && _list) {
        uint32_t cnt = lv_obj_get_child_cnt(_list);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_group_remove_obj(lv_obj_get_child(_list, i));
        }
        lv_group_remove_obj(_clearBtn);
        lv_group_remove_obj(_closeBtn);
    }
    // Clear any stale visual state. PRESSED can persist if a click handler
    // ran a screen transition before LVGL dispatched the release event;
    // FOCUSED can linger across show/hide cycles in some LVGL paths.
    lv_obj_clear_state(_clearBtn, LV_STATE_FOCUSED | LV_STATE_PRESSED);
    lv_obj_clear_state(_closeBtn, LV_STATE_FOCUSED | LV_STATE_PRESSED);
    lv_obj_clean(_list);

    int n = cache.count();
    if (n == 0) {
        lv_obj_clear_flag(_emptyHint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_clearBtn, LV_OBJ_FLAG_HIDDEN);  // nothing to clear
        if (grp) {
            lv_group_add_obj(grp, _closeBtn);
            lv_group_focus_obj(_closeBtn);
        }
        return;
    }
    lv_obj_add_flag(_emptyHint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_clearBtn, LV_OBJ_FLAG_HIDDEN);

    // Sort indices newest-first by lastHeardMs (wrap-safe via signed diff)
    int order[HEARD_ADVERT_CAP];
    for (int i = 0; i < n; i++) order[i] = i;
    const HeardAdvert* es = cache.entries();
    std::sort(order, order + n, [es](int a, int b) {
        return (int32_t)(es[a].lastHeardMs - es[b].lastHeardMs) > 0;
    });

    for (int oi = 0; oi < n; oi++) {
        int slot = order[oi];
        const HeardAdvert& e = es[slot];

        lv_obj_t* row = lv_obj_create(_list);
        lv_obj_set_size(row, 316, LV_SIZE_CONTENT);  // matches ConvoListScreen, leaves room for scrollbar
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_set_style_bg_color(row, theme::ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        if (grp) lv_group_add_obj(grp, row);
        lv_obj_add_event_cb(row, [](lv_event_t* ev) {
            lv_obj_scroll_to_view(lv_event_get_target(ev), LV_ANIM_ON);
        }, LV_EVENT_FOCUSED, nullptr);

        // Stash the slot index in user_data (no allocation, no delete cleanup needed)
        lv_obj_set_user_data(row, (void*)(intptr_t)slot);
        lv_obj_add_event_cb(row, rowClickCb, LV_EVENT_CLICKED, this);

        const Contact* known = contacts.findByPublicKey(e.pubKey);
        bool queued = e.savePending && !known;
        if (!queued && !known) {
            // Catches the cache-evicted-but-already-in-config case so the
            // row visual stays consistent with what openDetail will show.
            char pubHex[65];
            for (int i = 0; i < 32; i++) sprintf(pubHex + i * 2, "%02x", e.pubKey[i]);
            pubHex[64] = '\0';
            queued = ConfigManager::instance().hasContactByPubkeyHex(String(pubHex));
        }

        // Type icon — replaced with refresh glyph for queued (saved-this-session)
        // entries so the user sees something is pending without opening the modal.
        lv_obj_t* icon = lv_label_create(row);
        lv_obj_set_style_text_font(icon, FONT_NORMAL, 0);
        if (queued) {
            lv_obj_set_style_text_color(icon, theme::OFFGRID_ACCENT, 0);
            lv_label_set_text(icon, LV_SYMBOL_REFRESH);
        } else {
            lv_obj_set_style_text_color(icon, typeColor(e.type), 0);
            lv_label_set_text(icon, typeIcon(e.type));
        }

        // Name (alias if known; show heard name in parens if it differs)
        lv_obj_t* name = lv_label_create(row);
        lv_obj_set_style_text_font(name, FONT_NORMAL, 0);
        lv_obj_set_style_text_color(name,
            (known || queued) ? theme::TEXT_TIMESTAMP : theme::TEXT_PRIMARY, 0);
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        String label = " ";
        if (known) {
            label += known->name;
            if (e.name[0] != '\0' && known->name != e.name) {
                char akaBuf[80];
                snprintf(akaBuf, sizeof(akaBuf), t("heard_aka_fmt"), e.name);
                label += " ";
                label += akaBuf;
            }
        } else if (e.name[0] != '\0') {
            label += e.name;
        } else {
            label += pubKeyToShortId(e.pubKey);
        }
        lv_label_set_text(name, label.c_str());

        // Hops
        lv_obj_t* hopsLbl = lv_label_create(row);
        lv_obj_set_style_text_font(hopsLbl, FONT_SMALL, 0);
        lv_obj_set_style_text_color(hopsLbl, theme::TEXT_SECONDARY, 0);
        String hopsText = " " + formatHops(e.hops) + " ";
        lv_label_set_text(hopsLbl, hopsText.c_str());

        // Age
        lv_obj_t* age = lv_label_create(row);
        lv_obj_set_style_text_font(age, FONT_SMALL, 0);
        lv_obj_set_style_text_color(age, theme::TEXT_TIMESTAMP, 0);
        lv_label_set_text(age, formatAge(e.lastHeardMs).c_str());
    }

    // Group: trackball cycles rows → clear → close → rows
    if (grp) {
        lv_group_add_obj(grp, _clearBtn);
        lv_group_add_obj(grp, _closeBtn);

        // Restore focus by pubkey if possible; otherwise top row, otherwise close button
        bool restored = false;
        if (hadFocus) {
            uint32_t cnt = lv_obj_get_child_cnt(_list);
            for (uint32_t i = 0; i < cnt; i++) {
                lv_obj_t* child = lv_obj_get_child(_list, i);
                int slot = (int)(intptr_t)lv_obj_get_user_data(child);
                if (slot >= 0 && slot < cache.count() &&
                    memcmp(cache.entries()[slot].pubKey, focusedKey, 32) == 0) {
                    lv_group_focus_obj(child);
                    lv_obj_scroll_to_view(child, LV_ANIM_OFF);
                    restored = true;
                    break;
                }
            }
        }
        if (!restored) {
            if (lv_obj_get_child_cnt(_list) > 0) {
                lv_group_focus_obj(lv_obj_get_child(_list, 0));
            } else {
                lv_group_focus_obj(_closeBtn);
            }
        }
    }
}

void HeardAdvertsScreen::tick() {
    if (!_screen || lv_obj_has_flag(_screen, LV_OBJ_FLAG_HIDDEN)) return;
    if (_detailMsgbox) return;  // don't disrupt an open modal

    uint32_t v = HeardAdvertCache::instance().version();
    if (v == _lastVersion) return;

    uint32_t now = millis();
    if (now - _lastRebuildMs < 1000) return;  // 1Hz cap

    _lastVersion   = v;
    _lastRebuildMs = now;
    rebuild();
}

void HeardAdvertsScreen::openDetail(int slotIdx) {
    auto& cache = HeardAdvertCache::instance();
    if (slotIdx < 0 || slotIdx >= cache.count()) return;
    const HeardAdvert& e = cache.entries()[slotIdx];
    auto& contacts = ContactStore::instance();
    const Contact* known = contacts.findByPublicKey(e.pubKey);

    _detailSlot = slotIdx;

    // Decide save state. Save is offered ONLY for CHAT-type adverts —
    // repeaters, rooms, and sensors aren't messageable peers and don't
    // belong in the contact list.
    auto& cm = ConfigManager::instance();
    char pubHex[65];
    for (int i = 0; i < 32; i++) sprintf(pubHex + i * 2, "%02x", e.pubKey[i]);
    pubHex[64] = '\0';

    const bool isChat        = (e.type == ADV_TYPE_CHAT);
    const bool atCap         = ((int)cm.config().contacts.size() >= defaults::MAX_CHAT_CONTACTS);
    // "queued" combines two paths: the in-memory savePending flag (set
    // when we just saved this entry) and a config check (covers the case
    // where the cache evicted the just-saved entry and the user re-heard
    // the same node; appendDiscoveredContact would refuse, so we surface
    // the state up-front instead of letting Save fail silently).
    const bool queued        = e.savePending || cm.hasContactByPubkeyHex(String(pubHex));
    const bool savable       = isChat && !known && !queued && !atCap;

    if (savable) _detailMode = DETAIL_SAVABLE;
    else         _detailMode = DETAIL_INFO;

    // Build detail body. Persist as member to keep pointer stable for LVGL.
    // Order: human-readable identity first, transport details next, raw key last.
    _detailText = "";
    _detailText += typeLabel(e.type);
    _detailText += "\n";
    if (known) {
        _detailText += t("heard_alias_label");
        _detailText += known->name;
        _detailText += "\n";
    }
    if (e.name[0] != '\0') {
        _detailText += t("heard_name_label");
        _detailText += e.name;
        _detailText += "\n";
    }
    _detailText += t("heard_hops_label");
    _detailText += formatHops(e.hops);
    _detailText += "\n";
    if (e.pathByteLen > 0) {
        _detailText += t("heard_path_label");
        _detailText += formatPath(e.pathBytes, e.pathByteLen, e.hashSize);
        _detailText += "\n";
    }
    if (e.hasGps) {
        char gpsBuf[40];
        snprintf(gpsBuf, sizeof(gpsBuf), t("heard_gps_fmt"),
                 e.gpsLat / 1e6, e.gpsLon / 1e6);
        _detailText += gpsBuf;
    }
    _detailText += t("heard_heard_label");
    _detailText += formatAge(e.lastHeardMs);

    // Status line — surfaces why Save isn't offered (only for CHAT entries
    // that the user might reasonably expect to be savable).
    if (isChat && !known) {
        if (queued) {
            _detailText += "\n";
            _detailText += t("heard_status_queued");
        } else if (atCap) {
            _detailText += "\n";
            _detailText += t("heard_buffer_full");
        }
    }
    // (Key block is rendered as a separate, smaller-font label below.)

    // Buttons depend on save state.
    static const char* btns_savable[3] = { nullptr, nullptr, "" };
    static const char* btns_info[2]    = { nullptr, "" };
    const char** btns;
    if (_detailMode == DETAIL_SAVABLE) {
        btns_savable[0] = t("heard_btn_save");
        btns_savable[1] = "OK";
        btns = btns_savable;
    } else {
        btns_info[0] = "OK";
        btns = btns_info;
    }

    _detailMsgbox = lv_msgbox_create(NULL, t("heard_adverts_title"),
                                     _detailText.c_str(), btns, false);
    lv_obj_center(_detailMsgbox);
    lv_obj_set_width(_detailMsgbox, 280);
    lv_obj_set_height(_detailMsgbox, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(_detailMsgbox, 216, 0);
    lv_obj_set_style_bg_color(_detailMsgbox, theme::BG_SECONDARY, 0);
    lv_obj_set_style_text_color(_detailMsgbox, theme::TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(_detailMsgbox, FONT_NORMAL, 0);

    // Uniform vertical breathing room between every body line — matches the
    // implicit gap between the title bar and the first body line.
    lv_obj_t* body = lv_msgbox_get_text(_detailMsgbox);
    if (body) lv_obj_set_style_text_line_space(body, 4, 0);

    // Key block: separate label so we can use a smaller, dimmer font without
    // affecting the human-readable fields above.
    String keyText = t("heard_key_label");
    keyText += "\n";
    keyText += formatKeyChunked(e.pubKey);

    lv_obj_t* keyLabel = lv_label_create(_detailMsgbox);
    lv_obj_set_style_text_font(keyLabel, FONT_SMALL, 0);
    lv_obj_set_style_text_color(keyLabel, theme::TEXT_SECONDARY, 0);
    lv_obj_set_style_text_line_space(keyLabel, 2, 0);
    lv_obj_set_style_pad_top(keyLabel, 2, 0);
    lv_label_set_text(keyLabel, keyText.c_str());

    // Insert the key label just before the button matrix. Look up the btnm's
    // actual index instead of hardcoding 2 — robust against any future LVGL
    // change to msgbox internal child layout.
    lv_obj_t* btnm = lv_msgbox_get_btns(_detailMsgbox);
    if (btnm) {
        lv_obj_move_to_index(keyLabel, lv_obj_get_index(btnm));
        UIManager::instance().switchToModalGroup(btnm);
    }

    lv_obj_add_event_cb(_detailMsgbox, detailBtnCb, LV_EVENT_VALUE_CHANGED, this);
}

void HeardAdvertsScreen::closeDetail() {
    if (!_detailMsgbox) return;
    UIManager::instance().restoreFromModalGroup();
    lv_msgbox_close(_detailMsgbox);
    _detailMsgbox = nullptr;
    _detailText   = "";
    _detailSlot   = -1;
    _detailMode   = DETAIL_INFO;
}

void HeardAdvertsScreen::closeBtnCb(lv_event_t* e) {
    UIManager::instance().goHome();
}

void HeardAdvertsScreen::clearBtnCb(lv_event_t* e) {
    HeardAdvertsScreen* self = (HeardAdvertsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    HeardAdvertCache::instance().clear();
    // The tick() rate-limit would otherwise delay the visual response to the
    // tap by up to a second; rebuild immediately for instant feedback.
    self->_lastVersion   = HeardAdvertCache::instance().version();
    self->_lastRebuildMs = millis();
    self->rebuild();
}

void HeardAdvertsScreen::rowClickCb(lv_event_t* e) {
    HeardAdvertsScreen* self = (HeardAdvertsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    int slot = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    self->openDetail(slot);
}

void HeardAdvertsScreen::detailBtnCb(lv_event_t* e) {
    HeardAdvertsScreen* self = (HeardAdvertsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_detailMsgbox) return;
    uint16_t btnIdx = lv_msgbox_get_active_btn(self->_detailMsgbox);
    if (btnIdx == LV_BTNMATRIX_BTN_NONE) return;

    // Dispatch by mode. Index 0 in 2-button modes is the action; the
    // trailing button is always OK/close.
    if (self->_detailMode == DETAIL_SAVABLE && btnIdx == 0) {
        self->handleSave();
        return;
    }
    if (self->_detailMode == DETAIL_SAVED && btnIdx == 0) {
        // Reboot now. Mirrors AdminScreen::offgridToggleCb.
        UIManager::instance().restoreFromModalGroup();
        delay(200);
        ESP.restart();
        return;
    }
    self->closeDetail();
}

void HeardAdvertsScreen::handleSave() {
    if (_detailSlot < 0 || _detailSlot >= HeardAdvertCache::instance().count()) {
        closeDetail();
        return;
    }
    const HeardAdvert& e = HeardAdvertCache::instance().entries()[_detailSlot];

    ContactConfig cc;
    if (e.name[0] != '\0') {
        cc.alias = e.name;
    } else {
        cc.alias = String("node_") + pubKeyToShortId(e.pubKey).substring(0, 8);
    }
    // ContactStore parses publicKey as 64 contiguous hex chars (or base64).
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", e.pubKey[i]);
    hex[64] = '\0';
    cc.publicKey = String(hex);
    // Conservative defaults — user reviews via the config tool. The
    // from_discovery flag tags this entry as auto-added.
    cc.allowTelemetry   = false;
    cc.allowLocation    = false;
    cc.allowEnvironment = false;
    cc.alwaysSound      = false;
    cc.allowSos         = false;
    cc.sendSos          = false;
    cc.fromDiscovery    = true;

    bool ok = ConfigManager::instance().appendDiscoveredContact(cc);
    if (!ok) {
        // openDetail's gating means cap/queue cases shouldn't reach here —
        // by construction, Save is only offered when those don't apply. So
        // this is realistically an SD I/O error. Surface it instead of
        // closing silently.
        Serial.println("[HeardAdverts] save failed");
        closeDetail();
        showSimpleInfoModal(t("heard_save_failed"));
        return;
    }
    HeardAdvertCache::instance().markSavePending(e.pubKey);
    closeDetail();
    showSavedConfirmation();
}

void HeardAdvertsScreen::showSimpleInfoModal(const char* msg) {
    _detailMode = DETAIL_INFO;
    _detailText = msg;

    static const char* btns[2] = { "OK", "" };
    _detailMsgbox = lv_msgbox_create(NULL, t("heard_adverts_title"),
                                     _detailText.c_str(), btns, false);
    lv_obj_center(_detailMsgbox);
    lv_obj_set_width(_detailMsgbox, 280);
    lv_obj_set_height(_detailMsgbox, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(_detailMsgbox, theme::BG_SECONDARY, 0);
    lv_obj_set_style_text_color(_detailMsgbox, theme::TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(_detailMsgbox, FONT_NORMAL, 0);

    lv_obj_t* btnm = lv_msgbox_get_btns(_detailMsgbox);
    if (btnm) UIManager::instance().switchToModalGroup(btnm);

    lv_obj_add_event_cb(_detailMsgbox, detailBtnCb, LV_EVENT_VALUE_CHANGED, this);
}

void HeardAdvertsScreen::showSavedConfirmation() {
    _detailMode = DETAIL_SAVED;
    _detailText = t("heard_saved_msg");

    static const char* btns[3] = { nullptr, nullptr, "" };
    btns[0] = t("heard_btn_reboot");
    btns[1] = "OK";

    _detailMsgbox = lv_msgbox_create(NULL, t("heard_adverts_title"),
                                     _detailText.c_str(), btns, false);
    lv_obj_center(_detailMsgbox);
    lv_obj_set_width(_detailMsgbox, 280);
    lv_obj_set_height(_detailMsgbox, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(_detailMsgbox, theme::BG_SECONDARY, 0);
    lv_obj_set_style_text_color(_detailMsgbox, theme::TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(_detailMsgbox, FONT_NORMAL, 0);

    lv_obj_t* btnm = lv_msgbox_get_btns(_detailMsgbox);
    if (btnm) UIManager::instance().switchToModalGroup(btnm);

    lv_obj_add_event_cb(_detailMsgbox, detailBtnCb, LV_EVENT_VALUE_CHANGED, this);
}

}  // namespace mclite
