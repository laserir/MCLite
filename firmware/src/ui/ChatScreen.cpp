#include "ChatScreen.h"
#include "UIManager.h"
#include "theme.h"
#include "../hal/Display.h"
#include "../hal/GPS.h"
#include "../storage/MessageStore.h"
#include "../config/ConfigManager.h"
#include "../config/defaults.h"
#include "../mesh/ContactStore.h"
#include "../util/TextSanitizer.h"
#include "../util/coordparse.h"
#include "../storage/TileLoader.h"
#include "../i18n/I18n.h"
#include "../util/TimeHelper.h"

namespace mclite {

namespace {
struct RetryData { String text; uint32_t packetId; };
struct MapCoord  { double lat; double lon; };
}  // namespace

void ChatScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(parent);
    lv_obj_set_size(_screen, Display::width(),
                    Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT);
    lv_obj_align(_screen, LV_ALIGN_BOTTOM_MID, 0, -theme::FOOTER_HEIGHT);
    lv_obj_set_style_bg_color(_screen, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_set_style_radius(_screen, 0, 0);
    lv_obj_set_style_pad_all(_screen, 0, 0);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    createHeader();
    createChatArea();
    createInputBar();

#ifdef PLATFORM_TWATCH
    // Right-swipe anywhere on Chat → return home. T-Watch is touch-only;
    // mirrors the iOS back-swipe gesture. No-op on T-Deck (gated).
    // GESTURE_BUBBLE must be CLEARED so LVGL stops the gesture-bubble walk
    // here and actually dispatches LV_EVENT_GESTURE to _screen (otherwise
    // the walk runs past every ancestor to NULL and no event is sent).
    // Suppressed while the on-screen keyboard is up so finger-drags across
    // keys (especially when releasing on a right-edge key) don't escape to
    // navigation.
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(_screen, [](lv_event_t* e) {
        auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
        if (self->_kbd && !lv_obj_has_flag(self->_kbd, LV_OBJ_FLAG_HIDDEN)) return;
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_RIGHT) UIManager::instance().goHome();
    }, LV_EVENT_GESTURE, this);
#endif

    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void ChatScreen::createHeader() {
    _win = lv_win_create(_screen, theme::CHAT_HEADER_HEIGHT);
    lv_obj_set_size(_win, Display::width(),
                    Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT
                    - theme::CHAT_INPUT_HEIGHT);
    lv_obj_align(_win, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(_win, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(_win, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_win, 0, 0);
    lv_obj_set_style_radius(_win, 0, 0);
    lv_obj_set_style_pad_all(_win, 0, 0);

    lv_obj_t* header = lv_win_get_header(_win);
    lv_obj_set_style_bg_color(header, theme::BG_STATUS_BAR(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_hor(header, theme::CHAT_HEADER_PAD_HOR, 0);

    // Back button
    lv_obj_t* backBtn = lv_win_add_btn(_win, LV_SYMBOL_LEFT, theme::BTN_HEADER_BACK_W);
    lv_obj_set_style_bg_opa(backBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(backBtn, 0, 0);
    lv_obj_set_style_border_width(backBtn, 0, 0);
    lv_obj_add_event_cb(backBtn, backBtnCb, LV_EVENT_CLICKED, this);
    lv_obj_t* backImg = lv_obj_get_child(backBtn, 0);
    lv_obj_set_style_text_font(backImg, FONT_HEADING, 0);
    lv_obj_set_style_text_color(backImg, theme::ACCENT(), 0);

    // Contact/channel name
    _headerName = lv_win_add_title(_win, "");
    lv_obj_set_style_text_font(_headerName, FONT_HEADING, 0);
    lv_obj_set_style_text_color(_headerName, theme::TEXT_PRIMARY(), 0);

    // Map button — shown only for DM conversations
    _mapBtn = lv_win_add_btn(_win, LV_SYMBOL_GPS, theme::BTN_HEADER_ICON_W);
    lv_obj_set_style_bg_opa(_mapBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(_mapBtn, 0, 0);
    lv_obj_set_style_border_width(_mapBtn, 0, 0);
    lv_obj_set_style_pad_all(_mapBtn, 0, 0);
    lv_obj_add_event_cb(_mapBtn, mapBtnCb, LV_EVENT_CLICKED, this);
    lv_obj_t* mapImg = lv_obj_get_child(_mapBtn, 0);
    lv_obj_set_style_text_font(mapImg, FONT_HEADING, 0);
    lv_obj_set_style_text_color(mapImg, theme::TEXT_SECONDARY(), 0);
    lv_obj_add_flag(_mapBtn, LV_OBJ_FLAG_HIDDEN);

    // Telemetry button — shown only for DM conversations
    _telemBtn = lv_win_add_btn(_win, LV_SYMBOL_REFRESH, theme::BTN_HEADER_ICON_W);
    lv_obj_set_style_bg_opa(_telemBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(_telemBtn, 0, 0);
    lv_obj_set_style_border_width(_telemBtn, 0, 0);
    lv_obj_set_style_pad_all(_telemBtn, 0, 0);
    lv_obj_add_event_cb(_telemBtn, telemBtnCb, LV_EVENT_CLICKED, this);
    lv_obj_t* telemImg = lv_obj_get_child(_telemBtn, 0);
    lv_obj_set_style_text_font(telemImg, FONT_HEADING, 0);
    lv_obj_set_style_text_color(telemImg, theme::TEXT_SECONDARY(), 0);
    lv_obj_add_flag(_telemBtn, LV_OBJ_FLAG_HIDDEN);

    // Share button — re-broadcasts the contact's advert (DM only). Revealed by
    // setShareAvailable() when an advert blob exists and the feature is enabled.
    _shareBtn = lv_win_add_btn(_win, LV_SYMBOL_UPLOAD, theme::BTN_HEADER_ICON_W);
    lv_obj_set_style_bg_opa(_shareBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(_shareBtn, 0, 0);
    lv_obj_set_style_border_width(_shareBtn, 0, 0);
    lv_obj_set_style_pad_all(_shareBtn, 0, 0);
    lv_obj_add_event_cb(_shareBtn, shareBtnCb, LV_EVENT_CLICKED, this);
    lv_obj_t* shareImg = lv_obj_get_child(_shareBtn, 0);
    lv_obj_set_style_text_font(shareImg, FONT_HEADING, 0);
    lv_obj_set_style_text_color(shareImg, theme::TEXT_SECONDARY(), 0);
    lv_obj_add_flag(_shareBtn, LV_OBJ_FLAG_HIDDEN);

    // Mute indicator — shown on the right of the header when chat is muted
    _muteIcon = lv_win_add_btn(_win, LV_SYMBOL_MUTE, theme::BTN_HEADER_ICON_W);
    lv_obj_set_style_bg_opa(_muteIcon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(_muteIcon, 0, 0);
    lv_obj_set_style_border_width(_muteIcon, 0, 0);
    lv_obj_set_style_pad_all(_muteIcon, 0, 0);
    lv_obj_add_event_cb(_muteIcon, muteIconCb, LV_EVENT_CLICKED, this);
    lv_obj_t* muteImg = lv_obj_get_child(_muteIcon, 0);
    lv_obj_set_style_text_font(muteImg, FONT_HEADING, 0);
    lv_obj_set_style_text_color(muteImg, theme::TEXT_SECONDARY(), 0);
    lv_obj_add_flag(_muteIcon, LV_OBJ_FLAG_HIDDEN);
}

void ChatScreen::createChatArea() {
    _chatArea = lv_win_get_content(_win);
    lv_obj_set_style_bg_opa(_chatArea, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_chatArea, 0, 0);
    lv_obj_set_style_pad_all(_chatArea, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_row(_chatArea, theme::PAD_SMALL, 0);
#ifdef PLATFORM_TWATCH
    lv_obj_set_style_pad_hor(_chatArea, theme::SAFE_AREA_LEFT, 0);
#endif
    lv_obj_set_flex_flow(_chatArea, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(_chatArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_chatArea, LV_DIR_VER);
}

void ChatScreen::createInputBar() {
    _inputBar = lv_obj_create(_screen);
    lv_obj_set_size(_inputBar, Display::width(), theme::CHAT_INPUT_HEIGHT);
    lv_obj_align(_inputBar, LV_ALIGN_BOTTOM_MID, 0, -theme::SAFE_AREA_BOTTOM);
    lv_obj_set_style_bg_color(_inputBar, theme::BG_INPUT(), 0);
    lv_obj_set_style_bg_opa(_inputBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_inputBar, 0, 0);
    lv_obj_set_style_radius(_inputBar, 0, 0);
    lv_obj_set_style_pad_all(_inputBar, theme::PAD_SMALL, 0);
    // Inner horizontal padding — full-width bg, content inset from edges.
    lv_obj_set_style_pad_hor(_inputBar, theme::INPUT_BAR_PAD_HOR, 0);
    lv_obj_clear_flag(_inputBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_inputBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_inputBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_inputBar, theme::PAD_SMALL, 0);

    // Canned messages button (only if enabled)
    if (ConfigManager::instance().config().messaging.cannedMessages) {
        _cannedBtn = lv_btn_create(_inputBar);
        lv_obj_set_size(_cannedBtn, theme::BTN_ACTION_W, theme::BTN_ACTION_H);
        lv_obj_set_style_bg_opa(_cannedBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(_cannedBtn, 0, 0);
        lv_obj_set_style_border_width(_cannedBtn, 0, 0);
        lv_obj_set_style_pad_all(_cannedBtn, 0, 0);
        lv_obj_set_ext_click_area(_cannedBtn, 8);
        lv_obj_add_event_cb(_cannedBtn, cannedBtnCb, LV_EVENT_CLICKED, this);

        lv_obj_t* cannedLbl = lv_label_create(_cannedBtn);
        lv_label_set_text(cannedLbl, LV_SYMBOL_LIST);
        lv_obj_set_style_text_font(cannedLbl, FONT_HEADING, 0);
        lv_obj_set_style_text_color(cannedLbl, theme::TEXT_SECONDARY(), 0);
        lv_obj_center(cannedLbl);
    }

    // Emoji picker button — gated by display.emoji (on by default, can be turned
    // off). Received emoji always render (the chat font carries an emoji
    // fallback); this flag only controls whether you can compose them.
    if (ConfigManager::instance().config().display.emoji) {
        _emojiBtn = lv_btn_create(_inputBar);
        lv_obj_set_size(_emojiBtn, theme::BTN_ACTION_W, theme::BTN_ACTION_H);
        lv_obj_set_style_bg_opa(_emojiBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(_emojiBtn, 0, 0);
        lv_obj_set_style_border_width(_emojiBtn, 0, 0);
        lv_obj_set_style_pad_all(_emojiBtn, 0, 0);
        lv_obj_set_ext_click_area(_emojiBtn, 8);
        lv_obj_add_event_cb(_emojiBtn, emojiBtnCb, LV_EVENT_CLICKED, this);

        lv_obj_t* emojiLbl = lv_label_create(_emojiBtn);
        lv_label_set_text(emojiLbl, "\xF0\x9F\x99\x82");  // 🙂 (U+1F642)
        lv_obj_set_style_text_font(emojiLbl, FONT_HEADING, 0);
        lv_obj_center(emojiLbl);
    }

    // Text input
    _textarea = lv_textarea_create(_inputBar);
    lv_obj_set_flex_grow(_textarea, 1);
    lv_obj_set_height(_textarea, theme::CHAT_TEXTAREA_H);
    lv_textarea_set_one_line(_textarea, true);
    lv_textarea_set_max_length(_textarea, 160);  // MeshCore MAX_TEXT_LEN
    lv_textarea_set_placeholder_text(_textarea, t("chat_placeholder"));
    lv_obj_set_ext_click_area(_textarea, 8);
    lv_obj_set_style_text_font(_textarea, FONT_BODY, 0);
    lv_obj_set_style_text_color(_textarea, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_bg_color(_textarea, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_border_color(_textarea, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(_textarea, 1, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(_textarea, textareaCb, LV_EVENT_READY, this);

    // GPS location button
    _gpsBtn = lv_btn_create(_inputBar);
    lv_obj_set_size(_gpsBtn, theme::BTN_ACTION_W, theme::BTN_ACTION_H);
    lv_obj_set_style_bg_opa(_gpsBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(_gpsBtn, 0, 0);
    lv_obj_set_style_border_width(_gpsBtn, 0, 0);
    lv_obj_set_style_pad_all(_gpsBtn, 0, 0);
    lv_obj_set_ext_click_area(_gpsBtn, 8);
    lv_obj_add_event_cb(_gpsBtn, gpsBtnCb, LV_EVENT_CLICKED, this);

    lv_obj_t* gpsLbl = lv_label_create(_gpsBtn);
    lv_label_set_text(gpsLbl, LV_SYMBOL_GPS);
    lv_obj_set_style_text_font(gpsLbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(gpsLbl, theme::TEXT_SECONDARY(), 0);
    lv_obj_center(gpsLbl);

    // Send button
    _sendBtn = lv_btn_create(_inputBar);
    lv_obj_set_size(_sendBtn, theme::BTN_SEND_W, theme::BTN_SEND_H);
    lv_obj_set_style_bg_color(_sendBtn, theme::ACCENT(), 0);
    lv_obj_set_style_radius(_sendBtn, 4, 0);
    lv_obj_set_ext_click_area(_sendBtn, 8);
    lv_obj_add_event_cb(_sendBtn, sendBtnCb, LV_EVENT_CLICKED, this);

    lv_obj_t* sendLbl = lv_label_create(_sendBtn);
    lv_label_set_text(sendLbl, t("btn_send"));
    lv_obj_set_style_text_font(sendLbl, FONT_BODY, 0);
    lv_obj_center(sendLbl);

#ifdef PLATFORM_TWATCH
    // On-screen QWERTY keyboard for the touch-only T-Watch. Parented to
    // `_screen` so it z-orders above the chat area and input bar; anchored
    // to the bottom of `_screen`. Linked to `_textarea` so keys go directly
    // into the chat draft. Hidden by default — showKeyboard() also lifts
    // the input bar above the keyboard so the textarea stays visible.
    _kbd = lv_keyboard_create(_screen);
    lv_obj_set_size(_kbd, Display::width(), 200);
    lv_obj_align(_kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(_kbd, _textarea);
    lv_obj_add_flag(_kbd, LV_OBJ_FLAG_HIDDEN);
    // iOS-style enlarged popover above the pressed key. Must run BEFORE
    // setting NO_REPEAT — set_popovers calls update_ctrl_map which replaces
    // the entire ctrl_map with the LVGL default, wiping any flags set
    // beforehand.
    lv_keyboard_set_popovers(_kbd, true);
    // Disable long-press auto-repeat — holding a key would otherwise type it
    // every 100 ms (LVGL's LV_EVENT_LONG_PRESSED_REPEAT cycle).
    lv_btnmatrix_set_btn_ctrl_all(_kbd, LV_BTNMATRIX_CTRL_NO_REPEAT);

    // Pressed-state styling: brighter accent bg + bigger glyph so the
    // popover above the finger is legible.
    lv_obj_set_style_text_font(_kbd, FONT_BODY, LV_PART_ITEMS);
    lv_obj_set_style_text_font(_kbd, FONT_TITLE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(_kbd, theme::ACCENT(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(_kbd, theme::TEXT_ON_ACCENT(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_radius(_kbd, 4, LV_PART_ITEMS);
    // Tighter inter-key padding → bigger hit area per key. Also strips the
    // keyboard's outer padding so keys at the edge fill all the way out.
    lv_obj_set_style_pad_all(_kbd, 2, 0);
    lv_obj_set_style_pad_gap(_kbd, 2, 0);

    // Tap textarea → show keyboard
    lv_obj_add_event_cb(_textarea, [](lv_event_t* e) {
        auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
        self->showKeyboard();
    }, LV_EVENT_CLICKED, this);

    // Keyboard Enter (OK key) → send. Keyboard X (CANCEL) → just hide.
    // Re-apply NO_REPEAT only on VALUE_CHANGED (key press / mode switch):
    // doing it on every event including LV_EVENT_DRAW_* invalidates all 40+
    // keys per frame and tanks rendering perf.
    lv_obj_add_event_cb(_kbd, [](lv_event_t* e) {
        auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_kbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        }
        if (code == LV_EVENT_READY) {
            self->trySendCurrent();
            self->hideKeyboard();
        } else if (code == LV_EVENT_CANCEL) {
            self->hideKeyboard();
        }
    }, LV_EVENT_ALL, this);
#endif
}

#ifdef PLATFORM_TWATCH
void ChatScreen::showKeyboard() {
    if (!_kbd) return;
    constexpr int KBD_H = 200;
    lv_obj_clear_flag(_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(_inputBar, LV_ALIGN_BOTTOM_MID, 0, -KBD_H);
    lv_obj_set_height(_win,
        Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT
        - theme::CHAT_INPUT_HEIGHT - KBD_H);
    scrollToBottom();
}

void ChatScreen::hideKeyboard() {
    if (!_kbd) return;
    lv_obj_add_flag(_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(_inputBar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_height(_win,
        Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT
        - theme::CHAT_INPUT_HEIGHT);
    scrollToBottom();
}
#endif

void ChatScreen::open(const ConvoId& id) {
    _currentConvo.reset(new ConvoId(id));

    // Set header name
    Conversation* convo = MessageStore::instance().getConversation(id);
    bool ro = convo && convo->readOnly;
    if (convo) {
        String prefix;
        if (id.type == ConvoId::ROOM) {
            prefix = ICON_ROOM " ";
        } else if (id.type == ConvoId::CHANNEL) {
            prefix = convo->isPrivate ? ICON_PRIVATE " " : ICON_CHANNEL " ";
        } else {
            prefix = ICON_DM " ";
        }
        lv_label_set_text(_headerName, (prefix + convo->displayName).c_str());
    }

    // Hide or show input bar based on read-only flag
    if (ro) {
        lv_obj_add_flag(_inputBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(_win, Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT);
    } else {
        lv_obj_clear_flag(_inputBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(_win, Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT - theme::CHAT_INPUT_HEIGHT);
    }

    // DM-only header telemetry button. The map button is shown only once we
    // know a position for the contact (see setMapAvailable, driven by
    // UIManager) — otherwise the map would open with nothing to centre on.
    if (id.type == ConvoId::DM) lv_obj_clear_flag(_telemBtn, LV_OBJ_FLAG_HIDDEN);
    else                        lv_obj_add_flag(_telemBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_mapBtn, LV_OBJ_FLAG_HIDDEN);    // revealed by setMapAvailable()
    lv_obj_add_flag(_shareBtn, LV_OBJ_FLAG_HIDDEN);  // revealed by setShareAvailable()

    // Mark as read
    MessageStore::instance().markRead(id);

    updateGpsButtonColor();
    updateMuteIndicator();
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
    lv_obj_set_width(row, theme::CONTENT_WIDTH - 12);
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Bubble — measure text to decide if wrapping is needed.
    // Short text: bubble sizes to content. Long text: fixed width so label wraps.
    const lv_coord_t maxTextW = theme::BUBBLE_MAX_WIDTH - theme::BUBBLE_PAD * 2;
    lv_point_t textSize;
    lv_txt_get_size(&textSize, msg.text.c_str(), FONT_BODY,
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
        lv_obj_set_style_bg_color(bubble, theme::BUBBLE_SELF(), 0);
        lv_obj_align(bubble, LV_ALIGN_RIGHT_MID, 0, 0);
    } else {
        lv_obj_set_style_bg_color(bubble, theme::BUBBLE_THEM(), 0);
        lv_obj_align(bubble, LV_ALIGN_LEFT_MID, 0, 0);
    }
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);

    // Sender name (for channel and room messages, incoming only)
    if (!msg.fromSelf && msg.senderName.length() > 0 && _currentConvo &&
        (_currentConvo->type == ConvoId::CHANNEL ||
         _currentConvo->type == ConvoId::ROOM)) {
        lv_obj_t* sender = lv_label_create(bubble);
        lv_obj_set_style_text_font(sender, FONT_BODY, 0);
        lv_obj_set_style_text_color(sender, theme::ACCENT(), 0);
        lv_label_set_text(sender, msg.senderName.c_str());
        // Tap the name → prepend "@name " into the input (reply mention). The name
        // String is owned by the label and freed on delete; `this` rides the cb.
        lv_obj_add_flag(sender, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(sender, 6);
        lv_obj_set_user_data(sender, new String(msg.senderName));
        lv_obj_add_event_cb(sender, senderNameClickCb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(sender, [](lv_event_t* e) {
            delete static_cast<String*>(lv_obj_get_user_data(lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // Message text. Self-bubble text uses a dedicated token so it stays legible
    // on the (themed) self-bubble color; them-bubble text uses the primary color.
    lv_obj_t* text = lv_label_create(bubble);
    lv_obj_set_style_text_font(text, FONT_BODY, 0);
    lv_obj_set_style_text_color(text,
        msg.fromSelf ? theme::BUBBLE_SELF_TEXT() : theme::TEXT_PRIMARY(), 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    if (needsWrap) {
        lv_obj_set_width(text, maxTextW);
    } else {
        lv_obj_set_width(text, LV_SIZE_CONTENT);
    }
    // Sanitize for display: strip emoji variation selectors (tofu otherwise) and
    // normalize typographic quotes to ASCII (Montserrat lacks the glyphs).
    lv_label_set_text(text, sanitizeForDisplay(msg.text).c_str());

    // If the message contains a coordinate (decimal lat/lon or MGRS/UTMREF) and
    // map tiles are present, add a touch-only underlined "Open in map" link that
    // centers the map on it. Tile-gated like the telemetry modal's Map button
    // (UIManager::evalCanMap) — no tiles ⇒ no link. Touch-only: NOT added to the
    // encoder group (would break trackball nav). One link per message (the parser
    // returns the first coordinate; a "both"-format message links the decimal).
    GeoCoord gc = parseFirstGeoCoord(msg.text);
    if (gc.valid && TileLoader::instance().tilesAvailable()) {
        lv_obj_t* link = lv_label_create(bubble);
        lv_obj_set_style_text_font(link, FONT_SMALL, 0);
        lv_obj_set_style_text_color(link, theme::ACCENT(), 0);
        lv_obj_set_style_text_decor(link, LV_TEXT_DECOR_UNDERLINE, 0);
        lv_label_set_text(link, t("map_open"));
        lv_obj_add_flag(link, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(link, 8);

        auto* mc = new MapCoord{gc.lat, gc.lon};
        lv_obj_set_user_data(link, mc);
        lv_obj_add_event_cb(link, mapLinkCb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(link, [](lv_event_t* e) {
            delete static_cast<MapCoord*>(lv_obj_get_user_data(lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // Bottom line: timestamp + delivery status
    lv_obj_t* meta = lv_obj_create(bubble);
    lv_obj_set_size(meta, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta, 0, 0);
    lv_obj_set_style_pad_all(meta, 0, 0);
    lv_obj_clear_flag(meta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(meta, 4, 0);

    // Timestamp — show HH:MM in local time (auto-DST via POSIX TZ)
    if (msg.timestamp > 1700000000) {
        lv_obj_t* ts = lv_label_create(meta);
        lv_obj_set_style_text_font(ts, FONT_BODY, 0);
        lv_obj_set_style_text_color(ts,
            msg.fromSelf ? theme::BUBBLE_SELF_META() : theme::TEXT_TIMESTAMP(), 0);
        char timeStr[8];
        TimeHelper::instance().formatHHMM(msg.timestamp, timeStr, sizeof(timeStr));
        lv_label_set_text(ts, timeStr);
    }

    // Received hop count (xN) — opt-in via Messaging → Show Hop Count, received only.
    // 0 = heard directly (no relays) / self.
    if (!msg.fromSelf && ConfigManager::instance().config().messaging.showHopCount) {
        lv_obj_t* hop = lv_label_create(meta);
        lv_obj_set_style_text_font(hop, FONT_BODY, 0);
        lv_obj_set_style_text_color(hop, theme::TEXT_TIMESTAMP(), 0);
        if (msg.hops == 0) lv_label_set_text(hop, t("lbl_hop_direct"));   // heard directly, no relays
        else               lv_label_set_text_fmt(hop, "x%u", (unsigned)msg.hops);
    }

    // Delivery status (outgoing only)
    if (msg.fromSelf) {
        lv_obj_t* status = lv_label_create(meta);
        lv_obj_set_style_text_font(status, FONT_BODY, 0);
        switch (msg.status) {
            case MessageStatus::SENDING:
                lv_label_set_text(status, "...");
                lv_obj_set_style_text_color(status, theme::BUBBLE_SELF_META(), 0);
                break;
            case MessageStatus::SENT:
                lv_label_set_text(status, LV_SYMBOL_OK);
                lv_obj_set_style_text_color(status, theme::BUBBLE_SELF_META(), 0);
                break;
            case MessageStatus::DELIVERED:
                lv_label_set_text(status, LV_SYMBOL_OK LV_SYMBOL_OK);
                lv_obj_set_style_text_color(status, theme::ACCENT(), 0);
                break;
            case MessageStatus::FAILED: {
                lv_label_set_text(status, LV_SYMBOL_CLOSE);
                lv_obj_set_style_text_color(status, theme::BATTERY_LOW(), 0);
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

    // Heard-by-N-repeaters indicator for our sent channel messages (#39): ✓ then ↻N
    // (a repeater relayed it — weaker than the DM double-tick, which is a real ACK).
    if (msg.fromSelf && msg.repeaterCount > 0) {
        lv_obj_t* rep = lv_label_create(meta);
        lv_obj_set_style_text_font(rep, FONT_BODY, 0);
        lv_obj_set_style_text_color(rep, theme::BUBBLE_SELF_META(), 0);   // match the ✓ tick colour
        lv_label_set_text_fmt(rep, LV_SYMBOL_REFRESH " %u", (unsigned)msg.repeaterCount);
    }

    // Reaction chips: aggregate emoji by type, show "👍 2" or just "👍" if count=1.
    if (!msg.reactions.empty()) {
        struct EmojiCount { String emoji; uint8_t count = 0; };
        static constexpr uint8_t MAX_EMOJI_TYPES = 8;
        EmojiCount agg[MAX_EMOJI_TYPES];
        uint8_t aggLen = 0;
        for (const auto& r : msg.reactions) {
            bool found = false;
            for (uint8_t i = 0; i < aggLen; i++) {
                if (agg[i].emoji == r.emoji) { agg[i].count++; found = true; break; }
            }
            if (!found && aggLen < MAX_EMOJI_TYPES) {
                agg[aggLen].emoji = r.emoji;
                agg[aggLen].count = 1;
                aggLen++;
            }
        }

        lv_obj_t* reactRow = lv_obj_create(bubble);
        lv_obj_set_size(reactRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(reactRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(reactRow, 0, 0);
        lv_obj_set_style_pad_all(reactRow, 0, 0);
        lv_obj_clear_flag(reactRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(reactRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(reactRow, 4, 0);

        for (uint8_t i = 0; i < aggLen; i++) {
            lv_obj_t* chip = lv_label_create(reactRow);
            lv_obj_set_style_text_font(chip, FONT_BODY, 0);
            lv_obj_set_style_text_color(chip,
                msg.fromSelf ? theme::BUBBLE_SELF_META() : theme::TEXT_SECONDARY(), 0);
            if (agg[i].count > 1) {
                lv_label_set_text_fmt(chip, "%s %u", agg[i].emoji.c_str(), (unsigned)agg[i].count);
            } else {
                lv_label_set_text(chip, agg[i].emoji.c_str());
            }
        }
    }
}

void ChatScreen::addMessageToView(const Message& msg) {
    addBubble(msg);
    scrollToBottom();
}

void ChatScreen::scrollToBottom() {
    uint32_t child_cnt = lv_obj_get_child_cnt(_chatArea);
    if (child_cnt == 0) return;
    lv_obj_update_layout(_chatArea);
    lv_obj_t* last_child = lv_obj_get_child(_chatArea, child_cnt - 1);
    lv_obj_scroll_to_view(last_child, LV_ANIM_ON);
}

void ChatScreen::show() {
    if (!_screen) return;
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    scrollToBottom();
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
        if (_emojiBtnm) hideEmojiPicker();
        lv_group_t* grp = lv_group_get_default();
        if (grp) {
            lv_group_remove_obj(_sendBtn);
            lv_group_remove_obj(_gpsBtn);
            lv_group_remove_obj(_textarea);
            if (_cannedBtn) lv_group_remove_obj(_cannedBtn);
        }
    }
    lv_obj_clean(_chatArea);
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}


void ChatScreen::updateGpsButtonColor() {
    if (!_gpsBtn) return;
    lv_obj_t* lbl = lv_obj_get_child(_gpsBtn, 0);
    if (!lbl) return;
    switch (GPS::instance().fixStatus()) {
        case FixStatus::LIVE:
            lv_obj_set_style_text_color(lbl, theme::ACCENT(), 0);
            break;
        case FixStatus::LAST_KNOWN:
            lv_obj_set_style_text_color(lbl, theme::GPS_LAST_KNOWN(), 0);
            break;
        case FixStatus::NO_FIX:
            lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
            break;
    }
}

void ChatScreen::gpsBtnCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    if (!self->_currentConvo || !self->_textarea) return;

    // Update button color on each click (reflects fix status)
    self->updateGpsButtonColor();

    auto& gps = GPS::instance();
    if (gps.fixStatus() == FixStatus::NO_FIX) return;

    // Insert the location into the input so the user can add context and send it
    // with the normal Send button (mirrors the @mention insert + emoji picker).
    // Byte-guarded so it can't push the draft past the 160-byte mesh limit.
    String loc = "@ " + gps.formatLocationWithStatus();
    String cur = lv_textarea_get_text(self->_textarea);
    if (cur.length() == 0) self->_lastLocInsert = "";   // fresh draft → allow insert

    // No double-insert: skip if the draft still ends with the location we last
    // appended. Compare against the *stored* insert (not the freshly-formatted
    // one) — a live fix's coordinates and the age qualifier jitter between taps,
    // so re-formatting would never match. Typing more text after it breaks the
    // suffix match, so a later insert still works.
    if (self->_lastLocInsert.length() == 0 || !cur.endsWith(self->_lastLocInsert)) {
        String ins = (cur.length() > 0 && !cur.endsWith(" ")) ? (" " + loc) : loc;
        if ((int)(cur.length() + ins.length()) > defaults::MAX_MSG_BYTES) {
            UIManager::instance().showToast(t("msg_too_long"));
            return;
        }
        lv_textarea_add_text(self->_textarea, ins.c_str());
        self->_lastLocInsert = loc;
    }

#ifdef PLATFORM_TWATCH
    self->showKeyboard();                       // on-screen keyboard to edit/send
#else
    lv_group_focus_obj(self->_textarea);        // T-Deck: physical keyboard types here
#endif
}

void ChatScreen::trySendCurrent() {
    if (!_currentConvo || !_onSend) return;
    const char* raw = lv_textarea_get_text(_textarea);
    if (!raw || raw[0] == '\0') return;
    // Sanitize before measuring/sending: strip emoji variation selectors + normalize
    // typographic quotes to ASCII — keeps the mesh bytes clean and reclaims budget.
    String text = sanitizeForDisplay(String(raw));
    // length() = UTF-8 byte length (matches MeshCore's strlen() > MAX_TEXT_LEN check).
    // The textarea caps at 160 *characters*, but emoji/accents are multi-byte, so a
    // short-looking message can exceed the byte budget and would otherwise fail to
    // send silently (still drawing a FAILED bubble).
    if (text.length() > defaults::MAX_MSG_BYTES) {
        UIManager::instance().showToast(t("msg_too_long"));
        return;  // keep the user's text so they can trim it
    }
    _onSend(*_currentConvo, text);
    lv_textarea_set_text(_textarea, "");
}

void ChatScreen::sendBtnCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    self->trySendCurrent();
#ifdef PLATFORM_TWATCH
    self->hideKeyboard();
#endif
}

void ChatScreen::backBtnCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    if (self->_onBack) {
        self->close();
        self->_onBack();
    }
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

void ChatScreen::senderNameClickCb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    auto* name = static_cast<String*>(lv_obj_get_user_data(lv_event_get_target(e)));
    if (!self || !name || !self->_textarea) return;
    // Prepend "@name " to whatever's already typed, then bring up the keyboard.
    String cur = lv_textarea_get_text(self->_textarea);
    String mention = "@[" + *name + "] ";
    // Only prepend if this mention isn't already in the draft — repeated taps just
    // (re)focus the input instead of stacking "@name @name ...". Also skip if it
    // wouldn't fit the 160-char textarea limit, so we never silently truncate the
    // tail of what the user already typed.
    if (cur.indexOf(mention) < 0 &&
        (int)(mention.length() + cur.length()) <= 160) {
        lv_textarea_set_text(self->_textarea, (mention + cur).c_str());
    }
#ifdef PLATFORM_TWATCH
    self->showKeyboard();                       // bring up the on-screen keyboard
#else
    lv_group_focus_obj(self->_textarea);        // T-Deck: physical keyboard types here
#endif
}

void ChatScreen::retryBtnCb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_onRetry || !self->_currentConvo) return;

    auto* rd = static_cast<RetryData*>(lv_obj_get_user_data(lv_event_get_target(e)));
    if (!rd) return;

    self->_onRetry(*self->_currentConvo, rd->text, rd->packetId);
}

void ChatScreen::mapLinkCb(lv_event_t* e) {
    auto* mc = static_cast<MapCoord*>(lv_obj_get_user_data(lv_event_get_target(e)));
    if (!mc) return;
    // Open the map centered on the shared coordinate. UIManager defers the screen
    // change via lv_async_call (a touch cb must not change screens synchronously).
    UIManager::instance().openMapAt(mc->lat, mc->lon, t("map_shared_location"));
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

    // Per-conversation override: a contact / channel / room may carry its own
    // quick-reply list. When present (non-empty) it wins over the global list
    // entirely; otherwise fall back to the existing global / i18n logic. The
    // per-conversation texts are raw user strings (language-independent).
    const std::vector<String>* convoCanned = nullptr;
    if (_currentConvo) {
        switch (_currentConvo->type) {
            case ConvoId::DM:
                for (const auto& ct : ContactStore::instance().all()) {
                    if (ct.shortId() == _currentConvo->id) {
                        if (!ct.canned.empty()) convoCanned = &ct.canned;
                        break;
                    }
                }
                break;
            case ConvoId::CHANNEL:
                for (const auto& ch : cfg.channels) {
                    if (ch.name == _currentConvo->id) {
                        if (!ch.canned.empty()) convoCanned = &ch.canned;
                        break;
                    }
                }
                break;
            case ConvoId::ROOM:
                for (const auto& r : cfg.roomServers) {
                    if (r.publicKey.substring(0, 16) == _currentConvo->id) {
                        if (!r.canned.empty()) convoCanned = &r.canned;
                        break;
                    }
                }
                break;
        }
    }

    // Collect canned message texts
    // Store in static array so btnmatrix labels remain valid
    static const char* labels[9];  // max 8 + sentinel
    static String stored[8];       // keep String storage alive
    int count = 0;

    if (convoCanned) {
        // Per-conversation override list (unconditional — already non-empty)
        for (size_t i = 0; i < convoCanned->size() && i < 8; i++) {
            stored[count] = (*convoCanned)[i];
            labels[count] = stored[count].c_str();
            count++;
        }
    } else if (!custom.empty()) {
        // A configured global custom array wins over i18n defaults regardless of UI language —
        // it's the user's explicit, language-independent list. (Previously gated on English, so
        // non-English users never saw their custom array even when one was configured.)
        for (size_t i = 0; i < custom.size() && i < 8; i++) {
            stored[count] = custom[i];
            labels[count] = stored[count].c_str();
            count++;
        }
    } else {
        // No custom array: use the i18n strings (language-sensitive defaults)
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

    // Dark overlay — matches _screen's dimensions exactly
    _cannedOverlay = lv_obj_create(_screen);
    lv_obj_set_size(_cannedOverlay, Display::width(),
                    Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT);
    lv_obj_set_pos(_cannedOverlay, 0, 0);
    lv_obj_set_style_bg_color(_cannedOverlay, theme::SCRIM(), 0);
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
#ifdef PLATFORM_TWATCH
    lv_coord_t pickerH = count * 64 + 16;  // 64px per button on T-Watch for finger taps
#else
    lv_coord_t pickerH = count * 24 + 8;
#endif
    lv_coord_t maxH = Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT - 16;  // leave margin
    if (pickerH > maxH) pickerH = maxH;
    lv_obj_set_size(_cannedBtnm, theme::MODAL_TEXT_WIDTH, pickerH);
    lv_obj_align(_cannedBtnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(_cannedBtnm, FONT_HEADING, 0);
    lv_obj_set_style_text_color(_cannedBtnm, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_bg_color(_cannedBtnm, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(_cannedBtnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_cannedBtnm, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(_cannedBtnm, 1, 0);
    lv_obj_set_style_radius(_cannedBtnm, 8, 0);
    // Button items styling
    lv_obj_set_style_bg_color(_cannedBtnm, theme::BG_INPUT(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(_cannedBtnm, theme::TEXT_PRIMARY(), LV_PART_ITEMS);
    lv_obj_set_style_radius(_cannedBtnm, 4, LV_PART_ITEMS);
    // Focused button highlight
    lv_obj_set_style_bg_color(_cannedBtnm, theme::ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(_cannedBtnm, theme::TEXT_ON_ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);

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

// ───────────────────────── Emoji picker ─────────────────────────
// Mirrors the canned picker (overlay + grid btnmatrix + modal-group isolation).
// Adopted from the jason-s13r fork; gated by display.emoji and with a byte-budget
// guard on insert.
void ChatScreen::emojiBtnCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    if (self->_emojiBtnm) self->hideEmojiPicker();
    else                  self->showEmojiPicker();
}

void ChatScreen::showEmojiPicker() {
    // 6-wide grid of UTF-8 emoji (static so the btnmatrix can retain the pointers).
    // 🙂🙃🙁😎😐😶 / 😴🤬😡🤡👽👀 / 🖕👍👎🧭💯💗 / 🍽🍔🍺🎉💩🔥 / ☂🌥🌧❄☀🌜
    static const char* emojiMap[] = {
        "\xF0\x9F\x99\x82", "\xF0\x9F\x99\x83", "\xF0\x9F\x99\x81", "\xF0\x9F\x98\x8E", "\xF0\x9F\x98\x90", "\xF0\x9F\x98\xB6", "\n",
        "\xF0\x9F\x98\xB4", "\xF0\x9F\xA4\xAC", "\xF0\x9F\x98\xA1", "\xF0\x9F\xA4\xA1", "\xF0\x9F\x91\xBD", "\xF0\x9F\x91\x80", "\n",
        "\xF0\x9F\x96\x95", "\xF0\x9F\x91\x8D", "\xF0\x9F\x91\x8E", "\xF0\x9F\xA7\xAD", "\xF0\x9F\x92\xAF", "\xF0\x9F\x92\x97", "\n",
        "\xF0\x9F\x8D\xBD", "\xF0\x9F\x8D\x94", "\xF0\x9F\x8D\xBA", "\xF0\x9F\x8E\x89", "\xF0\x9F\x92\xA9", "\xF0\x9F\x94\xA5", "\n",
        "\xE2\x98\x82", "\xF0\x9F\x8C\xA5", "\xF0\x9F\x8C\xA7", "\xE2\x9D\x84", "\xE2\x98\x80", "\xF0\x9F\x8C\x9C", ""
    };

    _emojiOverlay = lv_obj_create(_screen);
    lv_obj_set_size(_emojiOverlay, Display::width(),
                    Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT);
    lv_obj_set_pos(_emojiOverlay, 0, 0);
    lv_obj_set_style_bg_color(_emojiOverlay, theme::SCRIM(), 0);
    lv_obj_set_style_bg_opa(_emojiOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_emojiOverlay, 0, 0);
    lv_obj_clear_flag(_emojiOverlay, LV_OBJ_FLAG_SCROLLABLE);

    _emojiBtnm = lv_btnmatrix_create(_screen);
    lv_btnmatrix_set_map(_emojiBtnm, emojiMap);
#ifdef PLATFORM_TWATCH
    lv_coord_t pickerH = 5 * 56 + 16;  // 5 rows, 56 px each for finger taps
#else
    lv_coord_t pickerH = 5 * 32 + 8;
#endif
    lv_obj_set_size(_emojiBtnm, theme::MODAL_TEXT_WIDTH, pickerH);
    lv_obj_align(_emojiBtnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(_emojiBtnm, FONT_HEADING, 0);
    lv_obj_set_style_text_color(_emojiBtnm, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_bg_color(_emojiBtnm, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(_emojiBtnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_emojiBtnm, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(_emojiBtnm, 1, 0);
    lv_obj_set_style_radius(_emojiBtnm, 8, 0);
    lv_obj_set_style_bg_color(_emojiBtnm, theme::BG_INPUT(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(_emojiBtnm, theme::TEXT_PRIMARY(), LV_PART_ITEMS);
    lv_obj_set_style_radius(_emojiBtnm, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(_emojiBtnm, theme::ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(_emojiBtnm, theme::TEXT_ON_ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(_emojiBtnm, emojiBtnmCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_emojiBtnm, [](lv_event_t* ev) {
        if (lv_event_get_key(ev) != LV_KEY_ESC) return;
        ChatScreen* cs = (ChatScreen*)lv_event_get_user_data(ev);
        cs->hideEmojiPicker();
        lv_group_t* grp = lv_group_get_default();
        if (grp) lv_group_focus_obj(cs->_textarea);
    }, LV_EVENT_KEY, this);

    lv_obj_add_flag(_emojiOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_emojiOverlay, [](lv_event_t* ev) {
        ChatScreen* cs = (ChatScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* ctx) {
            ChatScreen* s = (ChatScreen*)ctx;
            s->hideEmojiPicker();
            lv_group_t* grp = lv_group_get_default();
            if (grp) lv_group_focus_obj(s->_textarea);
        }, cs);
    }, LV_EVENT_CLICKED, this);

    UIManager::instance().switchToModalGroup(_emojiBtnm);
}

void ChatScreen::emojiBtnmCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    uint16_t idx = lv_btnmatrix_get_selected_btn(self->_emojiBtnm);
    if (idx == LV_BTNMATRIX_BTN_NONE) return;
    const char* emoji = lv_btnmatrix_get_btn_text(self->_emojiBtnm, idx);
    if (!emoji) return;

    const char* current = lv_textarea_get_text(self->_textarea);
    size_t curLen = current ? strlen(current) : 0;
    // Block an insert that would push the message past the 160-byte budget.
    if (curLen + strlen(emoji) > defaults::MAX_MSG_BYTES) {
        UIManager::instance().showToast(t("msg_too_long"));
    } else if (curLen > 0) {
        lv_textarea_set_cursor_pos(self->_textarea, LV_TEXTAREA_CURSOR_LAST);
        lv_textarea_add_text(self->_textarea, emoji);
    } else {
        lv_textarea_set_text(self->_textarea, emoji);
    }

    // Defer dismiss (touch fires VALUE_CHANGED mid event-chain; sync delete crashes).
    lv_async_call([](void* ctx) {
        ChatScreen* cs = (ChatScreen*)ctx;
        cs->hideEmojiPicker();
        lv_group_t* grp = lv_group_get_default();
        if (grp) lv_group_focus_obj(cs->_textarea);
    }, self);
}

void ChatScreen::hideEmojiPicker() {
    if (!_emojiBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    lv_obj_del_async(_emojiBtnm);
    _emojiBtnm = nullptr;
    lv_obj_del_async(_emojiOverlay);
    _emojiOverlay = nullptr;
}

void ChatScreen::updateMuteIndicator() {
    if (!_muteIcon || !_currentConvo) return;
    bool allowMute = ConfigManager::instance().config().messaging.allowMute;
    bool muted = allowMute && MessageStore::instance().isMuted(*_currentConvo);
    if (muted) {
        lv_obj_clear_flag(_muteIcon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_muteIcon, LV_OBJ_FLAG_HIDDEN);
    }
}

// Show the header map button only when the contact has a known position (DM
// only). Called by UIManager on chat open and whenever a location is learned.
void ChatScreen::setMapAvailable(bool avail) {
    if (!_mapBtn) return;
    bool isDM = _currentConvo && _currentConvo->type == ConvoId::DM;
    if (avail && isDM) lv_obj_clear_flag(_mapBtn, LV_OBJ_FLAG_HIDDEN);
    else               lv_obj_add_flag(_mapBtn, LV_OBJ_FLAG_HIDDEN);
}

void ChatScreen::mapBtnCb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_currentConvo || !self->_onMap) return;
    self->_onMap(*self->_currentConvo);
}

void ChatScreen::telemBtnCb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_currentConvo || !self->_onTelem) return;
    self->_onTelem(*self->_currentConvo);
}

// Show the header Share button only for a DM contact we can re-broadcast (an
// advert blob is held and the feature is enabled). Driven by UIManager.
void ChatScreen::setShareAvailable(bool avail) {
    if (!_shareBtn) return;
    bool isDM = _currentConvo && _currentConvo->type == ConvoId::DM;
    if (avail && isDM) lv_obj_clear_flag(_shareBtn, LV_OBJ_FLAG_HIDDEN);
    else               lv_obj_add_flag(_shareBtn, LV_OBJ_FLAG_HIDDEN);
}

void ChatScreen::shareBtnCb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_currentConvo || !self->_onShare) return;
    self->_onShare(*self->_currentConvo);
}

void ChatScreen::muteIconCb(lv_event_t* e) {
    ChatScreen* self = (ChatScreen*)lv_event_get_user_data(e);
    if (!self || !self->_currentConvo) return;

    // Unmute when tapping the mute icon
    MessageStore::instance().setMuted(*self->_currentConvo, false);
    self->updateMuteIndicator();
    if (self->_onMute) {
        self->_onMute(*self->_currentConvo, false);
    }
}

}  // namespace mclite
