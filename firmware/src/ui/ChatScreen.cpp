#include "ChatScreen.h"
#include "UIManager.h"
#include "theme.h"
#include "../hal/GPS.h"
#include "../storage/MessageStore.h"
#include "../config/ConfigManager.h"
#include "../i18n/I18n.h"

namespace mclite {

namespace {
struct RetryData { String text; uint32_t packetId; };
}  // namespace

void ChatScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(parent);
    lv_obj_set_size(_screen, 320, 240 - theme::STATUS_BAR_HEIGHT);
    lv_obj_align(_screen, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_screen, theme::BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_set_style_radius(_screen, 0, 0);
    lv_obj_set_style_pad_all(_screen, 0, 0);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    createHeader();
    createChatArea();
    createInputBar();

    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void ChatScreen::createHeader() {
    _header = lv_obj_create(_screen);
    lv_obj_set_size(_header, 320, 28);
    lv_obj_align(_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(_header, theme::BG_STATUS_BAR, 0);
    lv_obj_set_style_bg_opa(_header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_header, 0, 0);
    lv_obj_set_style_radius(_header, 0, 0);
    lv_obj_set_style_pad_all(_header, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(_header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Back button
    lv_obj_t* backBtn = lv_btn_create(_header);
    lv_obj_set_size(backBtn, 30, 20);
    lv_obj_set_style_bg_opa(backBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(backBtn, 0, 0);
    lv_obj_set_style_border_width(backBtn, 0, 0);
    lv_obj_add_event_cb(backBtn, backBtnCb, LV_EVENT_CLICKED, this);

    lv_obj_t* backLbl = lv_label_create(backBtn);
    lv_label_set_text(backLbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(backLbl, theme::ACCENT, 0);
    lv_obj_center(backLbl);

    // Contact/channel name — wrapped in a transparent button for tap detection
    // Touch-only: do NOT add to encoder group (breaks trackball navigation)
    lv_obj_t* nameBtn = lv_btn_create(_header);
    lv_obj_set_height(nameBtn, 20);
    lv_obj_set_style_bg_opa(nameBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(nameBtn, 0, 0);
    lv_obj_set_style_border_width(nameBtn, 0, 0);
    lv_obj_set_style_pad_all(nameBtn, 0, 0);
    lv_obj_set_flex_grow(nameBtn, 1);
    lv_obj_add_event_cb(nameBtn, headerNameCb, LV_EVENT_CLICKED, this);

    _headerName = lv_label_create(nameBtn);
    lv_obj_set_style_text_font(_headerName, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(_headerName, theme::TEXT_PRIMARY, 0);
    lv_label_set_text(_headerName, "");
}

void ChatScreen::createChatArea() {
    _chatArea = lv_obj_create(_screen);
    lv_obj_set_size(_chatArea, 320, 240 - theme::STATUS_BAR_HEIGHT - 28 - 36);
    lv_obj_align(_chatArea, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_opa(_chatArea, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_chatArea, 0, 0);
    lv_obj_set_style_pad_all(_chatArea, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_row(_chatArea, theme::PAD_SMALL, 0);
    lv_obj_set_flex_flow(_chatArea, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(_chatArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_chatArea, LV_DIR_VER);
}

void ChatScreen::createInputBar() {
    _inputBar = lv_obj_create(_screen);
    lv_obj_set_size(_inputBar, 320, 36);
    lv_obj_align(_inputBar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_inputBar, theme::BG_INPUT, 0);
    lv_obj_set_style_bg_opa(_inputBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_inputBar, 0, 0);
    lv_obj_set_style_radius(_inputBar, 0, 0);
    lv_obj_set_style_pad_all(_inputBar, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(_inputBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_inputBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_inputBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_inputBar, theme::PAD_SMALL, 0);

    // Canned messages button (only if enabled)
    if (ConfigManager::instance().config().messaging.cannedMessages) {
        _cannedBtn = lv_btn_create(_inputBar);
        lv_obj_set_size(_cannedBtn, 28, 28);
        lv_obj_set_style_bg_opa(_cannedBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(_cannedBtn, 0, 0);
        lv_obj_set_style_border_width(_cannedBtn, 0, 0);
        lv_obj_set_style_pad_all(_cannedBtn, 0, 0);
        lv_obj_add_event_cb(_cannedBtn, cannedBtnCb, LV_EVENT_CLICKED, this);

        lv_obj_t* cannedLbl = lv_label_create(_cannedBtn);
        lv_label_set_text(cannedLbl, LV_SYMBOL_LIST);
        lv_obj_set_style_text_color(cannedLbl, theme::TEXT_SECONDARY, 0);
        lv_obj_center(cannedLbl);
    }

    // Text input
    _textarea = lv_textarea_create(_inputBar);
    lv_obj_set_flex_grow(_textarea, 1);
    lv_obj_set_height(_textarea, 28);
    lv_textarea_set_one_line(_textarea, true);
    lv_textarea_set_max_length(_textarea, 160);  // MeshCore MAX_TEXT_LEN
    lv_textarea_set_placeholder_text(_textarea, t("chat_placeholder"));
    lv_obj_set_style_text_font(_textarea, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_textarea, theme::TEXT_PRIMARY, 0);
    lv_obj_set_style_bg_color(_textarea, theme::BG_SECONDARY, 0);
    lv_obj_set_style_border_color(_textarea, theme::ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(_textarea, 1, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(_textarea, textareaCb, LV_EVENT_READY, this);

    // GPS location button
    _gpsBtn = lv_btn_create(_inputBar);
    lv_obj_set_size(_gpsBtn, 28, 28);
    lv_obj_set_style_bg_opa(_gpsBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(_gpsBtn, 0, 0);
    lv_obj_set_style_border_width(_gpsBtn, 0, 0);
    lv_obj_set_style_pad_all(_gpsBtn, 0, 0);
    lv_obj_add_event_cb(_gpsBtn, gpsBtnCb, LV_EVENT_CLICKED, this);

    lv_obj_t* gpsLbl = lv_label_create(_gpsBtn);
    lv_label_set_text(gpsLbl, LV_SYMBOL_GPS);
    lv_obj_set_style_text_color(gpsLbl, theme::TEXT_SECONDARY, 0);
    lv_obj_center(gpsLbl);

    // Send button
    _sendBtn = lv_btn_create(_inputBar);
    lv_obj_set_size(_sendBtn, 50, 28);
    lv_obj_set_style_bg_color(_sendBtn, theme::ACCENT, 0);
    lv_obj_set_style_radius(_sendBtn, 4, 0);
    lv_obj_add_event_cb(_sendBtn, sendBtnCb, LV_EVENT_CLICKED, this);

    lv_obj_t* sendLbl = lv_label_create(_sendBtn);
    lv_label_set_text(sendLbl, t("btn_send"));
    lv_obj_set_style_text_font(sendLbl, FONT_SMALL, 0);
    lv_obj_center(sendLbl);
}

void ChatScreen::open(const ConvoId& id) {
    _currentConvo.reset(new ConvoId(id));

    // Set header name
    Conversation* convo = MessageStore::instance().getConversation(id);
    bool ro = convo && convo->readOnly;
    if (convo) {
        String prefix;
        if (id.type == ConvoId::CHANNEL) {
            prefix = convo->isPrivate ? ICON_PRIVATE " " : ICON_CHANNEL " ";
        } else {
            prefix = ICON_DM " ";
        }
        lv_label_set_text(_headerName, (prefix + convo->displayName).c_str());
    }

    // Hide or show input bar based on read-only flag
    if (ro) {
        lv_obj_add_flag(_inputBar, LV_OBJ_FLAG_HIDDEN);
        // Expand chat area to fill the space
        lv_obj_set_height(_chatArea, 240 - theme::STATUS_BAR_HEIGHT - 28);
    } else {
        lv_obj_clear_flag(_inputBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(_chatArea, 240 - theme::STATUS_BAR_HEIGHT - 28 - 36);
    }

    // Mark as read
    MessageStore::instance().markRead(id);

    updateGpsButtonColor();
    refresh();
    show();

    // Focus the textarea for typing (keyboard keypad indev sends chars
    // without needing editing mode; leaving editing off lets trackball
    // cycle between textarea/gps/send buttons)
    if (!ro) {
        lv_textarea_set_text(_textarea, "");
        lv_group_t* grp = lv_group_get_default();
        if (grp) {
            lv_group_focus_obj(_textarea);
        }
    }
}

void ChatScreen::close() {
    _currentConvo.reset();
    hide();
}

void ChatScreen::refresh() {
    if (!_currentConvo) return;
    lv_obj_clean(_chatArea);

    Conversation* convo = MessageStore::instance().getConversation(*_currentConvo);
    if (!convo) return;

    for (const auto& msg : convo->messages) {
        if (msg.text.length() > 0) {
            addBubble(msg);
        }
    }
    scrollToBottom();
}

void ChatScreen::addBubble(const Message& msg) {
    // Container for alignment
    lv_obj_t* row = lv_obj_create(_chatArea);
    lv_obj_set_width(row, 308);
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Bubble — measure text to decide if wrapping is needed.
    // Short text: bubble sizes to content. Long text: fixed width so label wraps.
    const lv_coord_t maxTextW = theme::BUBBLE_MAX_WIDTH - theme::BUBBLE_PAD * 2;
    lv_point_t textSize;
    lv_txt_get_size(&textSize, msg.text.c_str(), FONT_SMALL,
                    0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    bool needsWrap = (textSize.x > maxTextW);

    lv_obj_t* bubble = lv_obj_create(row);
    if (needsWrap) {
        lv_obj_set_width(bubble, theme::BUBBLE_MAX_WIDTH);
    } else {
        lv_obj_set_width(bubble, LV_SIZE_CONTENT);
    }
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, theme::BUBBLE_RADIUS, 0);
    lv_obj_set_style_pad_all(bubble, theme::BUBBLE_PAD, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);

    if (msg.fromSelf) {
        lv_obj_set_style_bg_color(bubble, theme::BUBBLE_SELF, 0);
        lv_obj_align(bubble, LV_ALIGN_RIGHT_MID, 0, 0);
    } else {
        lv_obj_set_style_bg_color(bubble, theme::BUBBLE_THEM, 0);
        lv_obj_align(bubble, LV_ALIGN_LEFT_MID, 0, 0);
    }
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);

    // Sender name (for channel messages, incoming only)
    if (!msg.fromSelf && msg.senderName.length() > 0 &&
        _currentConvo && _currentConvo->type == ConvoId::CHANNEL) {
        lv_obj_t* sender = lv_label_create(bubble);
        lv_obj_set_style_text_font(sender, FONT_SMALL, 0);
        lv_obj_set_style_text_color(sender, theme::ACCENT, 0);
        lv_label_set_text(sender, msg.senderName.c_str());
    }

    // Message text
    lv_obj_t* text = lv_label_create(bubble);
    lv_obj_set_style_text_font(text, FONT_SMALL, 0);
    lv_obj_set_style_text_color(text, theme::TEXT_PRIMARY, 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    if (needsWrap) {
        lv_obj_set_width(text, maxTextW);
    } else {
        lv_obj_set_width(text, LV_SIZE_CONTENT);
    }
    lv_label_set_text(text, msg.text.c_str());

    // Bottom line: timestamp + delivery status
    lv_obj_t* meta = lv_obj_create(bubble);
    lv_obj_set_size(meta, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta, 0, 0);
    lv_obj_set_style_pad_all(meta, 0, 0);
    lv_obj_clear_flag(meta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(meta, 4, 0);

    // Timestamp — show HH:MM if it's a real Unix epoch (no live GPS needed)
    if (msg.timestamp > 1700000000) {
        lv_obj_t* ts = lv_label_create(meta);
        lv_obj_set_style_text_font(ts, FONT_SMALL, 0);
        lv_obj_set_style_text_color(ts,
            msg.fromSelf ? theme::BUBBLE_SELF_META : theme::TEXT_TIMESTAMP, 0);
        uint32_t secOfDay = msg.timestamp % 86400;
        int h = (int)(secOfDay / 3600) + ConfigManager::instance().config().gpsClockOffset;
        if (h < 0) h += 24;
        else if (h >= 24) h -= 24;
        int m = (secOfDay % 3600) / 60;
        char timeStr[8];
        snprintf(timeStr, sizeof(timeStr), "%02u:%02u", (unsigned)h, (unsigned)m);
        lv_label_set_text(ts, timeStr);
    }

    // Delivery status (outgoing only)
    if (msg.fromSelf) {
        lv_obj_t* status = lv_label_create(meta);
        lv_obj_set_style_text_font(status, FONT_SMALL, 0);
        switch (msg.status) {
            case MessageStatus::SENDING:
                lv_label_set_text(status, "...");
                lv_obj_set_style_text_color(status, theme::BUBBLE_SELF_META, 0);
                break;
            case MessageStatus::SENT:
                lv_label_set_text(status, LV_SYMBOL_OK);
                lv_obj_set_style_text_color(status, theme::BUBBLE_SELF_META, 0);
                break;
            case MessageStatus::DELIVERED:
                lv_label_set_text(status, LV_SYMBOL_OK LV_SYMBOL_OK);
                lv_obj_set_style_text_color(status, theme::ACCENT, 0);
                break;
            case MessageStatus::FAILED: {
                lv_label_set_text(status, LV_SYMBOL_CLOSE);
                lv_obj_set_style_text_color(status, theme::BATTERY_LOW, 0);
                lv_obj_add_flag(status, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_ext_click_area(status, 15);

                auto* rd = new RetryData{msg.text, msg.packetId};
                lv_obj_set_user_data(status, rd);
                lv_obj_add_event_cb(status, retryBtnCb, LV_EVENT_CLICKED, this);
                lv_obj_add_event_cb(status, [](lv_event_t* e) {
                    delete static_cast<RetryData*>(lv_obj_get_user_data(lv_event_get_target(e)));
                }, LV_EVENT_DELETE, nullptr);
                break;
            }
        }
    }
}

void ChatScreen::addMessageToView(const Message& msg) {
    addBubble(msg);
    scrollToBottom();
}

void ChatScreen::scrollToBottom() {
    lv_obj_scroll_to_y(_chatArea, LV_COORD_MAX, LV_ANIM_ON);
}

void ChatScreen::show() {
    if (!_screen) return;
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    // Add interactive widgets to input group (skip if input bar is hidden / read-only)
    if (!lv_obj_has_flag(_inputBar, LV_OBJ_FLAG_HIDDEN)) {
        lv_group_t* grp = lv_group_get_default();
        if (grp) {
            if (_cannedBtn) lv_group_add_obj(grp, _cannedBtn);
            lv_group_add_obj(grp, _textarea);
            lv_group_add_obj(grp, _gpsBtn);
            lv_group_add_obj(grp, _sendBtn);
        }
    }
}

void ChatScreen::hide() {
    if (!_screen) return;
    // Remove widgets from input group so they don't steal focus while hidden
    // (only if input bar was visible — read-only channels never add them)
    if (!lv_obj_has_flag(_inputBar, LV_OBJ_FLAG_HIDDEN)) {
        if (_cannedBtnm) hideCannedPicker();
        lv_group_t* grp = lv_group_get_default();
        if (grp) {
            lv_group_remove_obj(_sendBtn);
            lv_group_remove_obj(_gpsBtn);
            lv_group_remove_obj(_textarea);
            if (_cannedBtn) lv_group_remove_obj(_cannedBtn);
        }
    }
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}


void ChatScreen::updateGpsButtonColor() {
    if (!_gpsBtn) return;
    lv_obj_t* lbl = lv_obj_get_child(_gpsBtn, 0);
    if (!lbl) return;
    switch (GPS::instance().fixStatus()) {
        case FixStatus::LIVE:
            lv_obj_set_style_text_color(lbl, theme::ACCENT, 0);
            break;
        case FixStatus::LAST_KNOWN:
            lv_obj_set_style_text_color(lbl, theme::GPS_LAST_KNOWN, 0);
            break;
        case FixStatus::NO_FIX:
            lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY, 0);
            break;
    }
}

void ChatScreen::gpsBtnCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    if (!self->_currentConvo || !self->_onSend) return;

    // Update button color on each click
    self->updateGpsButtonColor();

    auto& gps = GPS::instance();
    FixStatus status = gps.fixStatus();
    if (status == FixStatus::NO_FIX) return;

    String locStr = gps.formatLocationWithStatus();

    static const char* btns[3];
    btns[0] = t("btn_cancel");
    btns[1] = t("btn_location_send");
    btns[2] = "";

    // Title includes age warning for last-known position
    String locTitle = String(LV_SYMBOL_GPS " ") + t("location_title");
    if (status == FixStatus::LAST_KNOWN) {
        char ageBuf[32];
        uint32_t age = gps.fixAgeSeconds();
        if (age < 60)
            snprintf(ageBuf, sizeof(ageBuf), t("loc_last_known_s"), (int)age);
        else if (age < 3600)
            snprintf(ageBuf, sizeof(ageBuf), t("loc_last_known_m"), (int)(age / 60));
        else
            snprintf(ageBuf, sizeof(ageBuf), t("loc_last_known_h"), (int)(age / 3600));
        locStr += "\n\n" + String(ageBuf);
    }

    lv_obj_t* msgbox = lv_msgbox_create(NULL,
        locTitle.c_str(), locStr.c_str(), btns, false);
    lv_obj_center(msgbox);
    lv_obj_set_style_bg_color(msgbox, theme::BG_SECONDARY, 0);
    lv_obj_set_style_text_color(msgbox, theme::TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(msgbox, FONT_NORMAL, 0);

    // Switch trackball/keyboard to modal group so they can't navigate chat behind
    lv_obj_t* btnm = lv_msgbox_get_btns(msgbox);
    if (btnm) UIManager::instance().switchToModalGroup(btnm);

    // Store self pointer on the msgbox for the callback
    lv_obj_set_user_data(msgbox, self);

    lv_obj_add_event_cb(msgbox, [](lv_event_t* ev) {
        lv_obj_t* mbox = lv_event_get_current_target(ev);
        uint16_t btnIdx = lv_msgbox_get_active_btn(mbox);
        if (btnIdx == LV_BTNMATRIX_BTN_NONE) return;

        ChatScreen* cs = (ChatScreen*)lv_obj_get_user_data(mbox);

        if (btnIdx == 1 && cs && cs->_currentConvo && cs->_onSend) {
            // "Send" pressed — format location with @ prefix and age qualifier
            String msg = "@ " + GPS::instance().formatLocationWithStatus();
            cs->_onSend(*cs->_currentConvo, msg);
        }

        // Restore input group before closing modal
        UIManager::instance().restoreFromModalGroup();

        lv_msgbox_close(mbox);
    }, LV_EVENT_VALUE_CHANGED, NULL);
}

void ChatScreen::sendBtnCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    const char* text = lv_textarea_get_text(self->_textarea);
    if (text && strlen(text) > 0 && self->_currentConvo && self->_onSend) {
        self->_onSend(*self->_currentConvo, String(text));
        lv_textarea_set_text(self->_textarea, "");
    }
}

void ChatScreen::backBtnCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    if (self->_onBack) {
        self->close();
        self->_onBack();
    }
}

void ChatScreen::headerNameCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    if (!self->_currentConvo || self->_currentConvo->type != ConvoId::DM) return;
    if (self->_onInfo) self->_onInfo(*self->_currentConvo);
}

void ChatScreen::textareaCb(lv_event_t* e) {
    // Enter key pressed in textarea — same as send
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    const char* text = lv_textarea_get_text(self->_textarea);
    if (text && strlen(text) > 0 && self->_currentConvo && self->_onSend) {
        self->_onSend(*self->_currentConvo, String(text));
        lv_textarea_set_text(self->_textarea, "");
    }
}

void ChatScreen::retryBtnCb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_onRetry || !self->_currentConvo) return;

    auto* rd = static_cast<RetryData*>(lv_obj_get_user_data(lv_event_get_target(e)));
    if (!rd) return;

    self->_onRetry(*self->_currentConvo, rd->text, rd->packetId);
}

void ChatScreen::cannedBtnCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    if (self->_cannedBtnm) {
        self->hideCannedPicker();
    } else {
        self->showCannedPicker();
    }
}

void ChatScreen::showCannedPicker() {
    const auto& cfg = ConfigManager::instance().config();
    const auto& custom = cfg.messaging.cannedCustom;
    bool isEnglish = cfg.language.isEmpty();

    // Collect canned message texts
    // Store in static array so btnmatrix labels remain valid
    static const char* labels[9];  // max 8 + sentinel
    static String stored[8];       // keep String storage alive
    int count = 0;

    if (isEnglish && !custom.empty()) {
        // English + custom array: use ONLY the custom entries
        for (size_t i = 0; i < custom.size() && i < 8; i++) {
            stored[count] = custom[i];
            labels[count] = stored[count].c_str();
            count++;
        }
    } else {
        // Non-English (lang file wins) or English with no custom array (defaults)
        for (int i = 1; i <= 8; i++) {
            char key[12];
            snprintf(key, sizeof(key), "canned_%d", i);
            const char* text = t(key);
            if (strcmp(text, key) == 0) break;  // key not defined → stop
            stored[count] = text;
            labels[count] = stored[count].c_str();
            count++;
        }
    }

    if (count == 0) return;
    labels[count] = "";  // sentinel

    // Dark overlay
    _cannedOverlay = lv_obj_create(_screen);
    lv_obj_set_size(_cannedOverlay, 320, 240 - theme::STATUS_BAR_HEIGHT);
    lv_obj_set_pos(_cannedOverlay, 0, 0);
    lv_obj_set_style_bg_color(_cannedOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_cannedOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_cannedOverlay, 0, 0);
    lv_obj_clear_flag(_cannedOverlay, LV_OBJ_FLAG_SCROLLABLE);

    // Btnmatrix — one button per row (each label followed by "\n" except last)
    // Build map with newline separators for one-column layout
    static const char* btnMap[17];  // max 8 labels + 7 newlines + sentinel
    int mi = 0;
    for (int i = 0; i < count; i++) {
        if (i > 0) btnMap[mi++] = "\n";
        btnMap[mi++] = labels[i];
    }
    btnMap[mi] = "";  // sentinel

    _cannedBtnm = lv_btnmatrix_create(_screen);
    lv_btnmatrix_set_map(_cannedBtnm, btnMap);
    lv_coord_t pickerH = count * 24 + 8;  // 24px per button + padding
    lv_coord_t maxH = 240 - theme::STATUS_BAR_HEIGHT - 16;  // leave margin
    if (pickerH > maxH) pickerH = maxH;
    lv_obj_set_size(_cannedBtnm, 280, pickerH);
    lv_obj_align(_cannedBtnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(_cannedBtnm, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(_cannedBtnm, theme::TEXT_PRIMARY, 0);
    lv_obj_set_style_bg_color(_cannedBtnm, theme::BG_SECONDARY, 0);
    lv_obj_set_style_bg_opa(_cannedBtnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_cannedBtnm, theme::ACCENT, 0);
    lv_obj_set_style_border_width(_cannedBtnm, 1, 0);
    lv_obj_set_style_radius(_cannedBtnm, 8, 0);
    // Button items styling
    lv_obj_set_style_bg_color(_cannedBtnm, theme::BG_INPUT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(_cannedBtnm, theme::TEXT_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_radius(_cannedBtnm, 4, LV_PART_ITEMS);
    // Focused button highlight
    lv_obj_set_style_bg_color(_cannedBtnm, theme::ACCENT, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(_cannedBtnm, lv_color_white(), LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(_cannedBtnm, cannedBtnmCb, LV_EVENT_VALUE_CHANGED, this);
    // ESC key dismisses the picker without selecting
    lv_obj_add_event_cb(_cannedBtnm, [](lv_event_t* ev) {
        if (lv_event_get_key(ev) != LV_KEY_ESC) return;
        ChatScreen* cs = (ChatScreen*)lv_event_get_user_data(ev);
        cs->hideCannedPicker();
        lv_group_t* grp = lv_group_get_default();
        if (grp) lv_group_focus_obj(cs->_textarea);
    }, LV_EVENT_KEY, this);

    // Tap overlay to dismiss (deferred — same reason as cannedBtnmCb)
    lv_obj_add_flag(_cannedOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_cannedOverlay, [](lv_event_t* ev) {
        ChatScreen* cs = (ChatScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* ctx) {
            ChatScreen* s = (ChatScreen*)ctx;
            s->hideCannedPicker();
            lv_group_t* grp = lv_group_get_default();
            if (grp) lv_group_focus_obj(s->_textarea);
        }, cs);
    }, LV_EVENT_CLICKED, this);

    // Switch to modal group so trackball is isolated to btnmatrix
    UIManager::instance().switchToModalGroup(_cannedBtnm);
}

void ChatScreen::cannedBtnmCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    uint16_t idx = lv_btnmatrix_get_selected_btn(self->_cannedBtnm);
    if (idx == LV_BTNMATRIX_BTN_NONE) return;

    const char* text = lv_btnmatrix_get_btn_text(self->_cannedBtnm, idx);
    if (!text) return;

    lv_textarea_set_text(self->_textarea, text);

    // Defer dismiss — touch input fires VALUE_CHANGED mid-event-chain
    // (PRESSED→VALUE_CHANGED→RELEASED→CLICKED).  Synchronous group
    // deletion inside restoreFromModalGroup() crashes because LVGL still
    // references the modal group for the remaining touch events.
    lv_async_call([](void* ctx) {
        ChatScreen* cs = (ChatScreen*)ctx;
        cs->hideCannedPicker();
        lv_group_t* grp = lv_group_get_default();
        if (grp) lv_group_focus_obj(cs->_textarea);
    }, self);
}

void ChatScreen::hideCannedPicker() {
    if (!_cannedBtnm) return;

    UIManager::instance().restoreFromModalGroup();

    // Use del_async — this may be called from within the btnmatrix's
    // own event callback; synchronous delete would corrupt the event loop
    lv_obj_del_async(_cannedBtnm);
    _cannedBtnm = nullptr;
    lv_obj_del_async(_cannedOverlay);
    _cannedOverlay = nullptr;
}

}  // namespace mclite
