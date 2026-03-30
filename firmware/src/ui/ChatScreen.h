#pragma once

#include <lvgl.h>
#include <functional>
#include <memory>
#include "../storage/MessageStore.h"

namespace mclite {

using OnSendCallback  = std::function<void(const ConvoId& id, const String& text)>;
using OnBackCallback  = std::function<void()>;
using OnInfoCallback  = std::function<void(const ConvoId& id)>;
using OnRetryCallback = std::function<void(const ConvoId& id, const String& text, uint32_t oldPacketId)>;

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
    void onInfo(OnInfoCallback cb)   { _onInfo = cb; }
    void onRetry(OnRetryCallback cb) { _onRetry = cb; }

    const ConvoId* currentConvo() const { return _currentConvo.get(); }
    lv_obj_t* obj() { return _screen; }

private:
    lv_obj_t* _screen    = nullptr;
    lv_obj_t* _header    = nullptr;
    lv_obj_t* _chatArea  = nullptr;
    lv_obj_t* _inputBar  = nullptr;
    lv_obj_t* _textarea  = nullptr;
    lv_obj_t* _sendBtn   = nullptr;
    lv_obj_t* _gpsBtn    = nullptr;
    lv_obj_t* _cannedBtn     = nullptr;
    lv_obj_t* _cannedBtnm    = nullptr;  // btnmatrix picker overlay
    lv_obj_t* _cannedOverlay = nullptr;
    lv_obj_t* _headerName = nullptr;

    std::unique_ptr<ConvoId> _currentConvo;

    OnSendCallback  _onSend;
    OnBackCallback  _onBack;
    OnInfoCallback  _onInfo;
    OnRetryCallback _onRetry;

    void createHeader();
    void createChatArea();
    void createInputBar();
    void updateGpsButtonColor();
    void showCannedPicker();
    void hideCannedPicker();

    void addBubble(const Message& msg);
    void scrollToBottom();

    static void sendBtnCb(lv_event_t* e);
    static void gpsBtnCb(lv_event_t* e);
    static void backBtnCb(lv_event_t* e);
    static void textareaCb(lv_event_t* e);
    static void headerNameCb(lv_event_t* e);
    static void retryBtnCb(lv_event_t* e);
    static void cannedBtnCb(lv_event_t* e);
    static void cannedBtnmCb(lv_event_t* e);
};

}  // namespace mclite
