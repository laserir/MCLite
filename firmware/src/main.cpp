#include <Arduino.h>
#include "util/log.h"

#include "hal/boards/board.h"
#include "config/defaults.h"
#include "hal/Display.h"
#include "hal/Battery.h"
#include "hal/GPS.h"
#include "hal/Speaker.h"
#include "storage/SDCard.h"
#include "config/ConfigManager.h"
#include "hal/IInput.h"
#include "mesh/MeshManager.h"
#include "mesh/ContactStore.h"
#include "mesh/ChannelStore.h"
#include "storage/MessageStore.h"
#include "ui/UIManager.h"
#include "i18n/I18n.h"
#include "storage/TelemetryCache.h"
#include "util/TimeHelper.h"
#include "net/WiFiManager.h"
#include "companion/CompanionService.h"
#include <helpers/esp32/SerialWifiInterface.h>
#include <helpers/ArduinoSerialInterface.h>
#include <helpers/esp32/SerialBLEInterface.h>
#ifdef PLATFORM_TWATCH
#include <Wire.h>
#include "hal/twatch/Expander.h"
#include "hal/twatch/Pmu.h"
#include "hal/twatch/Rtc.h"
#include "hal/twatch/Haptic.h"
#endif

using namespace mclite;

// Companion transports (mutually exclusive — one client at a time). WiFi-TCP
// (port 5000) is bound lazily once WiFi is up; USB runs over the USB-CDC Serial
// and mutes debug logging while active (the binary protocol can't share the port).
static SerialWifiInterface g_companionWifi;
static bool g_companionWifiServerStarted = false;
static ArduinoSerialInterface g_companionUsb;
static bool g_companionUsbStarted = false;
static SerialBLEInterface g_companionBle;
static bool g_companionBleStarted = false;
static char g_bleName[24];   // mutable name buffer for SerialBLEInterface::begin

// Forward declarations
static void setupMeshCallbacks();
static void handleKeyShortcuts();
static void updateCompanion();

void setup() {
    Serial.begin(115200);
    delay(500);
    LOGF("\n=== MCLite v%s ===\n", MCLITE_VERSION);

    // Enable board power
#ifdef PLATFORM_TDECK
    pinMode(TDECK_POWER_EN, OUTPUT);
    digitalWrite(TDECK_POWER_EN, HIGH);
    delay(100);
#elif defined(PLATFORM_TWATCH)
    // I2C first — XL9555 + AXP2101 both live on the shared I2C bus.
    Wire.begin(TWATCH_I2C_SDA, TWATCH_I2C_SCL, 400000);
    delay(10);
    // Expander gates display power and the LoRa antenna RF switch.
    Expander::instance().init();
    // PMU enables ALDO2 (display rail) before LovyanGFX talks to the panel.
    Pmu::instance().init();
    // Battery-backed RTC — restored as soon as the I2C bus is alive so the
    // clock + log timestamps are correct from the very first frame, well
    // before GPS gets a fix. Sync after applyTimezone() below so DST is
    // applied correctly.
    Rtc::instance().init();
    Haptic::instance().init();
    delay(10);
#endif

    // 1. Display + LVGL (configures SPI bus via LovyanGFX)
    LOGLN("[Boot] Display...");
    Display::instance().init();
    Display::instance().setBrightness(180);

    // 2. Show boot screen immediately (before SD, so user sees something)
    Display::instance().showBootScreen();

    // 3. Battery (I2C, no SPI conflict)
    Battery::instance().init();

    // 4. SD Card + Config
    //    Show status before SD ops, then no more LVGL until SD is done
    Display::instance().setBootStatus("Reading SD card...");
    LOGLN("[Boot] SD Card...");
    bool sdOk = SDCard::instance().init();

    LOGLN("[Boot] Config...");
    auto configResult = ConfigManager::LoadResult::LOAD_NO_FILE;
    if (sdOk) {
        configResult = ConfigManager::instance().load();
        if (configResult == ConfigManager::LOAD_NO_FILE) {
            LOGLN("[Boot] First boot — generating config");
            ConfigManager::instance().generate();
            configResult = ConfigManager::instance().load();  // Re-load → LOAD_OK
        } else if (configResult == ConfigManager::LOAD_ERROR) {
            LOGLN("[Boot] Config corrupt — using defaults");
        }
    }

    // Apply config-dependent settings
    const auto& cfg = ConfigManager::instance().config();
    Display::instance().setBrightness(cfg.display.brightness);

    // Apply timezone for auto-DST (before UI init)
    mclite::TimeHelper::instance().applyTimezone();

#ifdef PLATFORM_TWATCH
    // Restore the system clock from the battery-backed RTC so the status-bar
    // clock and outgoing-packet timestamps are correct before GPS gets a fix.
    // syncSystemClock() is idempotent — when GPS later locks it overwrites
    // with the more accurate value.
    if (mclite::Rtc::instance().isValid()) {
        uint32_t rtcEpoch = mclite::Rtc::instance().getEpoch();
        if (rtcEpoch) mclite::TimeHelper::instance().syncSystemClock(rtcEpoch);
    }
#endif

    // Load language translations (before UI init)
    I18n::instance().init(cfg.language);

    // Show custom boot text if configured (updates existing boot screen label)
    if (cfg.display.bootText.length() > 0) {
        Display::instance().setBootText(cfg.display.bootText.c_str());
    }

    // 5. Input devices
#ifdef PLATFORM_TWATCH
    // Touch reset AFTER display init — panel reset noise glitches the
    // touch controller if done before. Timing per LilyGoWatchUltra.cpp.
    Expander::instance().writePin(TWATCH_EXP_TOUCH_RST, LOW);
    delay(20);
    Expander::instance().writePin(TWATCH_EXP_TOUCH_RST, HIGH);
    delay(60);
#endif
    IInput::instance().init();

    // 6. GPS
    if (cfg.gpsEnabled) {
        Display::instance().setBootStatus("GPS...");
        GPS::instance().init();
        GPS::instance().setLastKnownMaxAge(cfg.gpsLastKnownMaxAge);
    }

    // 7. Speaker
    Display::instance().setBootStatus("Speaker...");
    Speaker::instance().init();
    if (!cfg.soundEnabled) {
        Speaker::instance().setMuted(true);
    }

    // 8. UI — creates main screen (hidden, boot screen still active)
    Display::instance().setBootStatus("Starting...");
    UIManager::instance().init();

    // 9. Switch from boot screen to main UI, then clean up boot screen
    if (!sdOk) {
        LOGLN("[Boot] No SD card!");
        UIManager::instance().loadMainScreen();
        Display::instance().hideBootScreen();
        UIManager::instance().showSetupScreen(UIManager::NO_SD);
    } else if (configResult == ConfigManager::LOAD_NO_FILE) {
        LOGLN("[Boot] No config file!");
        UIManager::instance().loadMainScreen();
        Display::instance().hideBootScreen();
        UIManager::instance().showSetupScreen(UIManager::NO_CONFIG);
    } else if (configResult == ConfigManager::LOAD_ERROR) {
        LOGLN("[Boot] Config error!");
        UIManager::instance().loadMainScreen();
        Display::instance().hideBootScreen();
        UIManager::instance().showSetupScreen(UIManager::CONFIG_ERROR);
    } else {
        // Normal boot — init mesh (loads contacts/channels + radio)
        Display::instance().setBootStatus("Radio...");
#ifdef PLATFORM_TWATCH
        // LoRa antenna switch must be HIGH before any radio TX, otherwise
        // the SX1262 is disconnected from the antenna.
        Expander::instance().writePin(TWATCH_EXP_LORA_RF_SW, HIGH);
#endif
        MeshManager::instance().init();
        setupMeshCallbacks();

        // If device name is still default "MCLite" and we have a public key,
        // set dynamic name: "MCLite-" + first 8 hex chars of public key
        auto& cfgMut = ConfigManager::instance().config();
        if (cfgMut.deviceName == defaults::DEVICE_NAME && cfgMut.publicKey.length() >= 8) {
            cfgMut.deviceName = String("MCLite-") + cfgMut.publicKey.substring(0, 8);
            ConfigManager::instance().save();
            LOGF("[Boot] Device name set to: %s\n", cfgMut.deviceName.c_str());
        }

        // Ensure history directory exists
        SDCard::instance().mkdir("/mclite");
        SDCard::instance().mkdir(defaults::HISTORY_DIR);

        // Create conversations and load history
        auto& msgStore = MessageStore::instance();
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const Contact* c = contacts.findByIndex(i);
            ConvoId id{ConvoId::DM, c->shortId()};
            msgStore.ensureConversation(id, c->name, false);
            msgStore.loadHistory(id);
        }
        for (const auto& ch : ChannelStore::instance().all()) {
            ConvoId id{ConvoId::CHANNEL, ch.name};
            msgStore.ensureConversation(id, ch.name, ch.isPrivate(), ch.readOnly);
            msgStore.loadHistory(id);
        }
        UIManager::instance().loadMainScreen();
        Display::instance().hideBootScreen();
    }

    // 10. PIN lock
    if (configResult == ConfigManager::LOAD_OK) {
        const auto& sec = cfg.security;
        if (sec.lockMode == "pin" && sec.pinCode.length() >= 4) {
            UIManager::instance().showPinLock();
        }
    }

    // 11. Offer SD-card firmware install if a newer/different bin is present.
    //     No-ops when SD has none or a PIN lock is active.
    if (configResult == ConfigManager::LOAD_OK) {
        UIManager::instance().checkForSdFirmware();
        // 12. WiFi auto-update (if enabled): connect, check GitHub, prompt if newer.
        UIManager::instance().checkForWiFiUpdateOnBoot();
    }

    LOGLN("[Boot] Ready!");
}

void loop() {
    GPS::instance().update();
    if (GPS::instance().isTimeSynced()) {
        uint32_t gpsEpoch = GPS::instance().currentTimestamp();
        mclite::TimeHelper::instance().syncSystemClock(gpsEpoch);
#ifdef PLATFORM_TWATCH
        // Sync the RTC against GPS at most once per minute so the chip stays
        // accurate against a fresh reference, without thrashing I2C writes.
        static uint32_t lastRtcSync = 0;
        if (gpsEpoch && gpsEpoch - lastRtcSync >= 60) {
            mclite::Rtc::instance().setEpoch(gpsEpoch);
            lastRtcSync = gpsEpoch;
        }
#endif
    }
    // Fallback: when on WiFi without a GPS fix, sync the clock over NTP (no-op once
    // GPS has synced — GPS is checked first above and wins).
    mclite::TimeHelper::instance().maybeNtpSync();
    MeshManager::instance().update();
    updateCompanion();
    UIManager::instance().update();
    Speaker::instance().update();

    handleKeyShortcuts();
    IInput::instance().updatePress();
    UIManager::instance().updateKeyLockToggle();
    UIManager::instance().updateSOSHold();
    UIManager::instance().checkBatteryAlert();

    delay(5);
}

// Drive the WiFi companion link. Runs only when the user enables "WiFi Companion
// Mode" (WiFi setup screen) AND WiFi is connected. Auto-suspends if WiFi drops
// and resumes when it returns, without losing the user's choice.
static void updateCompanion() {
    auto& comp = CompanionService::instance();
    const bool wifiUp = WiFiManager::instance().isConnected();

    // Pick at most one transport (the switches are mutually exclusive): USB if
    // enabled, else WiFi when enabled AND connected.
    BaseSerialInterface* desired = nullptr;
    bool usbActive = false;
    if (comp.usbCompanionEnabled()) {
        if (!g_companionUsbStarted) { g_companionUsb.begin(Serial); g_companionUsbStarted = true; }
        desired = &g_companionUsb;
        usbActive = true;
    } else if (comp.bleCompanionEnabled()) {
        if (!g_companionBleStarted) {
            // BLE + WiFi can't share the radio/RAM. Fully tear WiFi down and give
            // the driver time to actually release its memory before BLEDevice::init
            // — otherwise the two stacks race and crash. One-time blocking pause,
            // only on the first BLE enable.
            WiFiManager::instance().disconnect();
            delay(600);
            uint32_t pin = comp.ensureBlePin();
            const String& dn = ConfigManager::instance().config().deviceName;
            strncpy(g_bleName, dn.c_str(), sizeof(g_bleName) - 1);
            g_bleName[sizeof(g_bleName) - 1] = 0;
            // BLEDevice::init is heavy + one-shot — call begin() once per boot,
            // then just toggle advertising via enable()/disable().
            g_companionBle.begin("MeshCore-", g_bleName, pin);
            g_companionBleStarted = true;
            comp.setBleInited(true);   // WiFi now blocked until reboot
            LOGF("[Companion] BLE advertising as MeshCore-%s (PIN %06lu)\n",
                 g_bleName, (unsigned long)pin);
        }
        desired = &g_companionBle;
    } else if (comp.wifiCompanionEnabled() && wifiUp) {
        if (!g_companionWifiServerStarted) {
            g_companionWifi.begin(5000);
            g_companionWifiServerStarted = true;
            LOGLN("[Companion] WiFi TCP server listening on :5000");
        }
        desired = &g_companionWifi;
    }

    // Mute debug logs iff USB owns the port. Set before begin() so nothing leaks
    // onto the binary stream.
    mclite::g_logMuted = usbActive;

    if (desired) comp.begin(desired);   // begin() is idempotent if already active on it
    else if (comp.active()) comp.end();
    comp.loop();
}

static void setupMeshCallbacks() {
    auto& mesh = MeshManager::instance();

    mesh.onMessage([](const String& senderName, const uint8_t* senderKey,
                       const String& text, uint32_t timestamp) {
        Contact* contact = ContactStore::instance().findByPublicKey(senderKey);
        if (!contact) return;
        ConvoId id{ConvoId::DM, contact->shortId()};
        Message msg;
        msg.fromSelf = false;
        msg.text = text;
        msg.timestamp = timestamp;
        msg.senderName = senderName;
        msg.status = MessageStatus::DELIVERED;
        UIManager::instance().onIncomingMessage(id, msg);
        ContactStore::instance().updateLastSeen(senderKey);
    });

    mesh.onGroupMessage([](uint8_t channelIdx, const String& senderName,
                            const String& text, uint32_t timestamp) {
        Channel* ch = ChannelStore::instance().findByIndex(channelIdx);
        if (!ch) return;
        ConvoId id{ConvoId::CHANNEL, ch->name};
        Message msg;
        msg.fromSelf = false;
        msg.text = text;
        msg.timestamp = timestamp;
        msg.senderName = senderName;
        msg.status = MessageStatus::DELIVERED;
        UIManager::instance().onIncomingMessage(id, msg);
    });

    mesh.onAck([](uint32_t packetId) {
        UIManager::instance().onAckReceived(packetId);
        CompanionService::instance().onAckConfirmed(packetId);   // -> SEND_CONFIRMED
    });

    mesh.onFail([](uint32_t packetId) {
        UIManager::instance().onMessageFailed(packetId);
        CompanionService::instance().onSendFailed(packetId);
    });

    mesh.onAdvert([](const uint8_t* senderKey) {
        ContactStore::instance().updateLastSeen(senderKey);
    });

    mesh.onTelemetry([](const uint8_t* pubKey, const TelemetryData& data) {
        TelemetryCache::instance().store(pubKey, data);
        ContactStore::instance().updateLastSeen(pubKey);
        UIManager::instance().updateTelemetryModal(pubKey);
    });

    mesh.onTelemetryRetry([](uint32_t newTimeoutMs) {
        UIManager::instance().onTelemetryRetry(newTimeoutMs);
    });

    mesh.onRoomMessage([](size_t roomIdx, const String& roomName,
                           const uint8_t* senderPrefix,
                           const String& text, uint32_t timestamp) {
        UIManager::instance().onRoomMessageReceived(roomIdx, roomName,
                                                     senderPrefix, text, timestamp);
    });

    mesh.onRoomLogin([](size_t roomIdx, const String& roomName,
                         uint8_t status, uint8_t permissions) {
        UIManager::instance().onRoomLoginResponse(roomIdx, roomName, status, permissions);
    });
}

static void handleKeyShortcuts() {
    if (!IInput::instance().has(InputCapability::Keyboard)) return;

    auto& ui = UIManager::instance();
    char key = IInput::instance().pollKey();
    if (key == 0) return;

    // Don't process shortcuts while PIN locked — keys go to PIN overlay via LVGL
    if (ui.isLocked()) {
        IInput::instance().clearKey();
        return;
    }

    // Don't process shortcuts while key-locked
    if (ui.isKeyLocked()) {
        IInput::instance().clearKey();
        return;
    }

    if (key == 0x1B && ui.currentScreen() != Screen::CONVO_LIST) {
        ui.goHome();
        IInput::instance().clearKey();
        return;
    }
    if (key == '0' && ui.currentScreen() == Screen::CONVO_LIST &&
        ConfigManager::instance().config().security.adminEnabled) {
        ui.showScreen(Screen::ADMIN);
        IInput::instance().clearKey();
        return;
    }
    if (ui.currentScreen() == Screen::CONVO_LIST && key >= '1' && key <= '9') {
        size_t idx = key - '1';
        auto convos = MessageStore::instance().getConversationsSorted();
        if (idx < convos.size()) {
            ui.openChat(convos[idx]->convoId);
        }
        IInput::instance().clearKey();
        return;
    }
    if (key == 0x0C && ui.currentScreen() == Screen::CHAT) {
        ui.insertLocation();
        IInput::instance().clearKey();
        return;
    }
    IInput::instance().clearKey();
}
