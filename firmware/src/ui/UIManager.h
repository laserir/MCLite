#pragma once

#include <lvgl.h>
#include "StatusBar.h"
#include "ConvoListScreen.h"
#include "ChatScreen.h"
#include "AdminScreen.h"
#include "DeviceSettingsScreen.h"
#include "HeardAdvertsScreen.h"
#include "WiFiSetupScreen.h"
#include "UsbSetupScreen.h"
#include "BleSetupScreen.h"
#include "MapScreen.h"
#include "../storage/MessageStore.h"

namespace mclite {

enum class Screen {
    CONVO_LIST,
    CHAT,
    ADMIN,
    DEVICE_SETTINGS,
    HEARD_ADVERTS,
    WIFI_SETUP,
    USB_SETUP,
    BLE_SETUP
};

class UIManager {
public:
    bool init();
    void loadMainScreen();  // Switch from boot screen to main UI
    void update();  // Call from main loop (LVGL timer + status bar refresh)

    void showScreen(Screen screen);
    void openChat(const ConvoId& id);
    void goHome();

    // Firmware update: scan SD for a board-matching firmware bin and, if a
    // newer/different one is found, prompt to install it. Call once after the
    // main screen loads.
    void checkForSdFirmware();

    // WiFi auto-update: if configured + enabled, connect, check GitHub for a
    // newer release, and prompt to download+install. Call after checkForSdFirmware().
    void checkForWiFiUpdateOnBoot();

    // Offer a WiFi-downloaded update (used by the boot check + WiFi setup screen).
    void showWiFiInstallModal(const String& version, const String& url);

    // Called when a new message arrives (from MeshManager callback)
    void onIncomingMessage(const ConvoId& id, const Message& msg);

    // Called when an ACK is received (DM delivered)
    void onAckReceived(uint32_t packetId);

    // Called when all retries exhausted (DM failed)
    void onMessageFailed(uint32_t packetId);

    // Send a message (from the chat UI or the WiFi companion). Sends over the
    // mesh AND records it in the store/UI, so companion-originated messages show
    // on-device too. Returns the packet ID (0 on failure).
    uint32_t handleSend(const ConvoId& id, const String& text);

    // Retry a failed DM
    void handleRetry(const ConvoId& id, const String& text, uint32_t oldPacketId);

    // Room callbacks (wired from main.cpp setupMeshCallbacks)
    void onRoomMessageReceived(size_t roomIdx, const String& roomName,
                                const uint8_t* senderPrefix /* 4 B */,
                                const String& text, uint32_t timestamp);
    void onRoomLoginResponse(size_t roomIdx, const String& roomName,
                              uint8_t status, uint8_t permissions);

    // Read-only accessors for AdminScreen Rooms section
    bool isRoomLoggedIn(size_t roomIdx) const {
        return roomIdx < MAX_ROOMS && _roomLoggedIn[roomIdx];
    }

    // Show persistent setup/error screen (blocks all interaction until reboot)
    enum SetupReason { NO_SD, NO_CONFIG, CONFIG_ERROR };
    void showSetupScreen(SetupReason reason);

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
    void onTelemetryRetry(uint32_t newTimeoutMs);

    // Map screen (opened from telemetry modal)
    void showMapScreen(const uint8_t* pubKey, double lat, double lon, const String& contactName);

    // Open the general map (own location + heard-node markers). From the GPS icon.
    void showGeneralMap();

    // Open the map centered on an arbitrary coordinate (no contact). Used by the
    // tap-to-map link on a GPS coordinate in a chat message. Deferred via async.
    void openMapAt(double lat, double lon, const String& name);
    // Same, but with the node's pubKey so the map shows its real marker
    // type/name (used by the Heard Adverts per-row map button). Deferred.
    void openMapAt(const uint8_t* pubKey, double lat, double lon, const String& name);

    // Brief auto-dismissing toast on top layer. Non-modal — doesn't steal
    // focus and disappears after `durationMs` (default 1500ms).
    void showToast(const char* msg, uint32_t durationMs = 1500);

    // Modal input group helpers (used by ChatScreen GPS modal too)
    void switchToModalGroup(lv_obj_t* modalWidget);
    void restoreFromModalGroup();

    Screen currentScreen() const { return _currentScreen; }
    lv_group_t* inputGroup() const { return _inputGroup; }
    lv_obj_t* mainScreen() const { return _mainScreen; }

    static UIManager& instance();

private:
    UIManager() = default;
    Screen _currentScreen = Screen::CONVO_LIST;

    StatusBar           _statusBar;
    ConvoListScreen     _convoList;
    ChatScreen          _chatScreen;
    AdminScreen         _adminScreen;
    DeviceSettingsScreen _deviceSettingsScreen;
    HeardAdvertsScreen  _heardAdvertsScreen;
    WiFiSetupScreen     _wifiSetupScreen;
    UsbSetupScreen      _usbSetupScreen;
    BleSetupScreen      _bleSetupScreen;

    lv_obj_t*  _mainScreen = nullptr;
    lv_group_t* _inputGroup = nullptr;
    uint32_t  _lastStatusUpdate = 0;
    uint32_t  _lastDimCheck = 0;
    uint32_t  _lastActivity = 0;
    uint32_t  _pekLastMs = 0;       // T-Watch PEK short-press time (double-press → screenshot)
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

    // Setup screen (NO_SD / NO_CONFIG / CONFIG_ERROR) takes over the display.
    // Auto-dim still applies (battery save), but auto-lock is suppressed —
    // user needs to recover the device, not be locked out.
    bool       _inSetupMode = false;

    // Modal input group — isolates trackball/keyboard to modal while open
    lv_group_t* _modalGroup = nullptr;

    // Firmware-update (SD install) modal state
    lv_obj_t* _fwBar = nullptr;            // progress bar during install
    String    _fwPath;                     // SD path of the bin being offered (SD install)
    String    _fwUrl;                      // download URL (WiFi install; empty = SD install)
    String    _fwVersion;                  // its parsed version
    bool      _fwPromptDismissed = false;  // don't re-prompt after Abort this session
    void showFirmwareInstallModal(const String& path, const String& version);
    void buildFwInstallModal();            // shared modal builder (uses _fwVersion)
    void doFirmwareInstall();
    static void fwModalBtnCb(lv_event_t* e);
    static void fwProgressCb(uint8_t percent, void* user);          // flash phase
    static void fwDownloadProgressCb(uint8_t percent, void* user);  // download phase (WiFi)

    // Telemetry modal state
    lv_obj_t*   _telemMsgbox = nullptr;
    String      _telemText;
    String      _telemContactId;
    uint32_t    _telemTimeout = 0;
    bool        _telemPending = false;
    const char* _telemBtns[4] = {nullptr, nullptr, nullptr, nullptr};

    void dismissTelemetryModal();
    bool evalCanMap(const uint8_t* pubKey) const;
    void buildTelemetryMsgbox(bool canMap);  // (re)creates the msgbox widget
    static void telemBtnCb(lv_event_t* e);

    // Map screen state
    MapScreen _mapScreen;
    double    _pendingMapLat = 0.0;
    double    _pendingMapLon = 0.0;
    String    _pendingMapName;
    uint8_t   _pendingMapKey[32] = {0};
    bool      _pendingMapHasKey = false;  // false = center on coord only (no contact marker)
    static void openMapAsync(void* user);
    static void openGeneralMapAsync(void* user);

    // ─── Room state (decisions #14, #15 from room-server-plan.md) ───
    static constexpr size_t MAX_ROOMS = 8;
    bool          _roomLoggedIn[MAX_ROOMS]       = {};   // RESP_SERVER_LOGIN_OK observed this session
    uint8_t       _loginAttempt[MAX_ROOMS]       = {};   // exponential-backoff counter for boot login retry
    unsigned long _nextLoginAttemptMs[MAX_ROOMS] = {};   // when next boot retry is allowed
    unsigned long _lastLoginMs[MAX_ROOMS]        = {};   // last loginRoom() fire (any path)
    unsigned long _lastRoomMsgMs[MAX_ROOMS]      = {};   // last received signed-room msg per room

    void roomLoginTick();              // backoff retry on not-logged-in rooms (boot path)
    void roomChatOpenRelogin(size_t roomIdx);   // decision #14: re-login on ROOM ChatScreen open
    void roomSilenceTick(size_t roomIdx);       // decision #15: silence-triggered re-login while ROOM chat foreground
};

}  // namespace mclite
