#pragma once

#include <lvgl.h>
#include "StatusBar.h"
#include "ConvoListScreen.h"
#include "ChatScreen.h"
#include "AdminScreen.h"
#include "../storage/MessageStore.h"

namespace mclite {

enum class Screen {
    CONVO_LIST,
    CHAT,
    ADMIN
};

class UIManager {
public:
    bool init();
    void loadMainScreen();  // Switch from boot screen to main UI
    void update();  // Call from main loop (LVGL timer + status bar refresh)

    void showScreen(Screen screen);
    void openChat(const ConvoId& id);
    void goHome();

    // Called when a new message arrives (from MeshManager callback)
    void onIncomingMessage(const ConvoId& id, const Message& msg);

    // Called when an ACK is received (DM delivered)
    void onAckReceived(uint32_t packetId);

    // Called when all retries exhausted (DM failed)
    void onMessageFailed(uint32_t packetId);

    // Send a message from the chat UI
    void handleSend(const ConvoId& id, const String& text);

    // Retry a failed DM
    void handleRetry(const ConvoId& id, const String& text, uint32_t oldPacketId);

    // Show persistent setup/error screen (blocks all interaction until reboot)
    enum SetupReason { NO_SD, NO_CONFIG, CONFIG_ERROR };
    void showSetupScreen(SetupReason reason);

    // Insert GPS location into chat
    void insertLocation();

    // SOS send via trackball long-press (call from main loop)
    void updateSOSHold();


    // Send SOS message to all contacts and channels
    void sendSOSToAll();

    // Battery low alert check (call from main loop, self-throttled)
    void checkBatteryAlert();

    // PIN lock
    void showPinLock();
    void dismissPinLock();
    bool isLocked() const { return _isLocked; }

    // Key lock (lightweight input lock — no PIN required)
    void engageKeyLock();
    void disengageKeyLock();
    bool isKeyLocked() const { return _keyLocked; }
    void updateKeyLockToggle();  // Call from main loop after updatePress()

    // Telemetry modal
    void showTelemetryModal(const ConvoId& id);
    void updateTelemetryModal(const uint8_t* pubKey);

    // Modal input group helpers (used by ChatScreen GPS modal too)
    void switchToModalGroup(lv_obj_t* modalWidget);
    void restoreFromModalGroup();

    Screen currentScreen() const { return _currentScreen; }

    static UIManager& instance();

private:
    UIManager() = default;
    Screen _currentScreen = Screen::CONVO_LIST;

    StatusBar       _statusBar;
    ConvoListScreen _convoList;
    ChatScreen      _chatScreen;
    AdminScreen     _adminScreen;

    lv_obj_t*  _mainScreen = nullptr;
    lv_group_t* _inputGroup = nullptr;
    uint32_t  _lastStatusUpdate = 0;
    uint32_t  _lastDimCheck = 0;
    uint32_t  _lastActivity = 0;
    bool      _dimmed = false;

    static constexpr uint32_t STATUS_UPDATE_MS   = 1000;
    static constexpr uint32_t CONVO_REFRESH_MS   = 10000;  // Refresh convo list every 10s
    uint32_t _lastConvoRefresh  = 0;

    // SOS alert state
    lv_obj_t* _sosMsgbox = nullptr;
    ConvoId   _sosConvoId{ConvoId::DM, ""};
    bool      _sosIsDM = false;
    int       _sosContactIndex = -1;
    String    _sosAlertText;  // Persist for LVGL label lifetime

    bool checkSOS(const ConvoId& id, const Message& msg);
    void showSOSAlert(const ConvoId& id, const Message& msg);
    void dismissSOSAlert(bool sendReply);
    static void sosButtonCb(lv_event_t* e);

    // Trackball hold thresholds (shared between key lock and SOS)
    static constexpr uint32_t KEY_LOCK_HOLD_MS = 1000;  // Key lock toggle after 1s
    static constexpr uint32_t SOS_HOLD_SHOW_MS = 2000;  // Show SOS countdown after 2s
    static constexpr uint32_t SOS_HOLD_SEND_MS = 6000;  // Send SOS after 6s total
    lv_obj_t* _sosCountdownLabel = nullptr;
    bool      _sosCountdownActive = false;
    bool      _sosSentThisHold = false;

    // Battery low alert state
    bool     _batteryAlertSent = false;
    uint32_t _lastBatteryCheck = 0;
    static constexpr uint32_t BATTERY_CHECK_MS = 30000;  // Check every 30s

    // PIN lock state
    bool       _isLocked = false;
    lv_obj_t*  _pinOverlay  = nullptr;
    lv_obj_t*  _pinDots     = nullptr;
    lv_obj_t*  _pinStatus   = nullptr;
    lv_group_t* _pinGroup   = nullptr;
    String    _pinBuffer;
    void onPinKey(uint32_t key);
    static void pinKeyCb(lv_event_t* e);
    void checkWake();  // Wake display on any input while dimmed

    // Key lock state
    bool       _keyLocked = false;
    bool       _keyLockActioned = false; // Already triggered lock/unlock for current hold
    lv_obj_t*  _keyLockOverlay = nullptr;
    void showKeyLockOverlay();
    void hideKeyLockOverlay();

    // Modal input group — isolates trackball/keyboard to modal while open
    lv_group_t* _modalGroup = nullptr;

    // Telemetry modal state
    lv_obj_t* _telemMsgbox = nullptr;
    String    _telemText;
    String    _telemContactId;
    uint32_t  _telemTimeout = 0;
    bool      _telemPending = false;

    void dismissTelemetryModal();
    static void telemBtnCb(lv_event_t* e);
};

}  // namespace mclite
