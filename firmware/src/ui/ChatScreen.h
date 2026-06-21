#pragma once

#include <lvgl.h>
#include <functional>
#include <memory>
#include "../storage/MessageStore.h"

namespace mclite {

using OnSendCallback  = std::function<void(const ConvoId& id, const String& text)>;
using OnBackCallback  = std::function<void()>;
using OnRetryCallback = std::function<void(const ConvoId& id, const String& text, uint32_t oldPacketId)>;
using OnMuteCallback  = std::function<void(const ConvoId& id, bool muted)>;
using OnMapCallback   = std::function<void(const ConvoId& id)>;
using OnTelemCallback = std::function<void(const ConvoId& id)>;

class ChatScreen {
public:
    void create(lv_obj_t* parent);
    void open(const ConvoId& id);
    void close();
    void show();
    void hide();

    void addMessageToView(const Message& msg);
    void refresh();  // Reload from MessageStore

    void onSend(OnSendCallback cb)   { _onSend = cb; }
    void onBack(OnBackCallback cb)   { _onBack = cb; }
    void onRetry(OnRetryCallback cb) { _onRetry = cb; }
    void onMute(OnMuteCallback cb)   { _onMute = cb; }
    void onMap(OnMapCallback cb)     { _onMap = cb; }
    void onTelem(OnTelemCallback cb) { _onTelem = cb; }

    // Show/hide the header map button (DM only) — driven by UIManager when a
    // contact's position is known, so the map never opens without a centre.
    void setMapAvailable(bool avail);

    const ConvoId* currentConvo() const { return _currentConvo.get(); }
    lv_obj_t* obj() { return _screen; }

private:
    lv_obj_t* _screen    = nullptr;
    lv_obj_t* _win       = nullptr;  // lv_win: owns header + scrollable content
    lv_obj_t* _chatArea  = nullptr;
    lv_obj_t* _inputBar  = nullptr;
    lv_obj_t* _textarea  = nullptr;
    lv_obj_t* _sendBtn   = nullptr;
    lv_obj_t* _gpsBtn    = nullptr;
    lv_obj_t* _cannedBtn     = nullptr;
    lv_obj_t* _cannedBtnm    = nullptr;  // btnmatrix picker overlay
    lv_obj_t* _cannedOverlay = nullptr;
    lv_obj_t* _emojiBtn      = nullptr;  // emoji picker button (only when display.emoji on)
    lv_obj_t* _emojiBtnm     = nullptr;  // emoji grid picker overlay
    lv_obj_t* _emojiOverlay  = nullptr;
    lv_obj_t* _headerName = nullptr;
    lv_obj_t* _mapBtn   = nullptr;  // Map button (DM only)
    lv_obj_t* _telemBtn = nullptr;  // Telemetry button (DM only)
    lv_obj_t* _muteIcon = nullptr;  // Mute indicator in header
#ifdef PLATFORM_TWATCH
    lv_obj_t* _kbd        = nullptr;  // T-Watch only: on-screen keyboard
#endif

    std::unique_ptr<ConvoId> _currentConvo;
    String _lastLocInsert;   // last "@ <coords>" appended via the GPS button (anti-double-insert)

    OnSendCallback  _onSend;
    OnBackCallback  _onBack;
    OnRetryCallback _onRetry;
    OnMuteCallback  _onMute;
    OnMapCallback   _onMap;
    OnTelemCallback _onTelem;

    void createHeader();
    void createChatArea();
    void createInputBar();
    void updateGpsButtonColor();
    void showCannedPicker();
#ifdef PLATFORM_TWATCH
    void showKeyboard();
    void hideKeyboard();
#endif
    void hideCannedPicker();
    void showEmojiPicker();
    void hideEmojiPicker();
    void updateMuteIndicator();
    // Read the textarea and send it — but only if it fits the byte budget.
    // Over-budget (e.g. emoji/accents push past MAX_MSG_BYTES) → toast + keep text.
    void trySendCurrent();

    void addBubble(const Message& msg);
    void scrollToBottom();

    static void sendBtnCb(lv_event_t* e);
    static void gpsBtnCb(lv_event_t* e);
    static void backBtnCb(lv_event_t* e);
    static void textareaCb(lv_event_t* e);
    static void senderNameClickCb(lv_event_t* e);
    static void retryBtnCb(lv_event_t* e);
    static void mapLinkCb(lv_event_t* e);
    static void cannedBtnCb(lv_event_t* e);
    static void cannedBtnmCb(lv_event_t* e);
    static void emojiBtnCb(lv_event_t* e);
    static void emojiBtnmCb(lv_event_t* e);
    static void muteIconCb(lv_event_t* e);
    static void mapBtnCb(lv_event_t* e);
    static void telemBtnCb(lv_event_t* e);
};

}  // namespace mclite
