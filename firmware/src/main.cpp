#include <Arduino.h>

#include "config/defaults.h"
#include "hal/Display.h"
#include "hal/Battery.h"
#include "hal/GPS.h"
#include "hal/Speaker.h"
#include "storage/SDCard.h"
#include "config/ConfigManager.h"
#include "input/Keyboard.h"
#include "input/Trackball.h"
#include "input/Touch.h"
#include "mesh/MeshManager.h"
#include "mesh/ContactStore.h"
#include "mesh/ChannelStore.h"
#include "storage/MessageStore.h"
#include "ui/UIManager.h"
#include "i18n/I18n.h"
#include "storage/TelemetryCache.h"
#include "util/TimeHelper.h"

using namespace mclite;

// Forward declarations
static void setupMeshCallbacks();
static void handleKeyShortcuts();

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("\n=== MCLite v%s ===\n", MCLITE_VERSION);

    // Enable board power
    pinMode(TDECK_POWER_EN, OUTPUT);
    digitalWrite(TDECK_POWER_EN, HIGH);
    delay(100);

    // 1. Display + LVGL (configures SPI bus via LovyanGFX)
    Serial.println("[Boot] Display...");
    Display::instance().init();
    Display::instance().setBrightness(180);

    // 2. Show boot screen immediately (before SD, so user sees something)
    Display::instance().showBootScreen();

    // 3. Battery (I2C, no SPI conflict)
    Battery::instance().init();

    // 4. SD Card + Config
    //    Show status before SD ops, then no more LVGL until SD is done
    Display::instance().setBootStatus("Reading SD card...");
    Serial.println("[Boot] SD Card...");
    bool sdOk = SDCard::instance().init();

    Serial.println("[Boot] Config...");
    auto configResult = ConfigManager::LoadResult::LOAD_NO_FILE;
    if (sdOk) {
        configResult = ConfigManager::instance().load();
        if (configResult == ConfigManager::LOAD_NO_FILE) {
            Serial.println("[Boot] First boot — generating config");
            ConfigManager::instance().generate();
            configResult = ConfigManager::instance().load();  // Re-load → LOAD_OK
        } else if (configResult == ConfigManager::LOAD_ERROR) {
            Serial.println("[Boot] Config corrupt — using defaults");
        }
    }

    // Apply config-dependent settings
    const auto& cfg = ConfigManager::instance().config();
    Display::instance().setBrightness(cfg.display.brightness);

    // Apply timezone for auto-DST (before UI init)
    mclite::TimeHelper::instance().applyTimezone();

    // Load language translations (before UI init)
    I18n::instance().init(cfg.language);

    // Show custom boot text if configured (updates existing boot screen label)
    if (cfg.display.bootText.length() > 0) {
        Display::instance().setBootText(cfg.display.bootText.c_str());
    }

    // 5. Input devices
    Display::instance().setBootStatus("Keyboard...");
    Keyboard::instance().init();

    Display::instance().setBootStatus("Trackball...");
    Trackball::instance().init();

    Display::instance().setBootStatus("Touchscreen...");
    Touch::instance().init();

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
        Serial.println("[Boot] No SD card!");
        UIManager::instance().loadMainScreen();
        Display::instance().hideBootScreen();
        UIManager::instance().showSetupScreen(UIManager::NO_SD);
    } else if (configResult == ConfigManager::LOAD_NO_FILE) {
        Serial.println("[Boot] No config file!");
        UIManager::instance().loadMainScreen();
        Display::instance().hideBootScreen();
        UIManager::instance().showSetupScreen(UIManager::NO_CONFIG);
    } else if (configResult == ConfigManager::LOAD_ERROR) {
        Serial.println("[Boot] Config error!");
        UIManager::instance().loadMainScreen();
        Display::instance().hideBootScreen();
        UIManager::instance().showSetupScreen(UIManager::CONFIG_ERROR);
    } else {
        // Normal boot — init mesh (loads contacts/channels + radio)
        Display::instance().setBootStatus("Radio...");
        MeshManager::instance().init();
        setupMeshCallbacks();

        // If device name is still default "MCLite" and we have a public key,
        // set dynamic name: "MCLite-" + first 8 hex chars of public key
        auto& cfgMut = ConfigManager::instance().config();
        if (cfgMut.deviceName == defaults::DEVICE_NAME && cfgMut.publicKey.length() >= 8) {
            cfgMut.deviceName = String("MCLite-") + cfgMut.publicKey.substring(0, 8);
            ConfigManager::instance().save();
            Serial.printf("[Boot] Device name set to: %s\n", cfgMut.deviceName.c_str());
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
        if (sec.pinEnabled && sec.pinCode.length() >= 4) {
            UIManager::instance().showPinLock();
        }
    }

    Serial.println("[Boot] Ready!");
}

void loop() {
    GPS::instance().update();
    if (GPS::instance().isTimeSynced()) {
        mclite::TimeHelper::instance().syncSystemClock(GPS::instance().currentTimestamp());
    }
    MeshManager::instance().update();
    UIManager::instance().update();
    Speaker::instance().update();

    handleKeyShortcuts();
    Trackball::instance().updatePress();
    UIManager::instance().updateKeyLockToggle();
    UIManager::instance().updateSOSHold();
    UIManager::instance().checkBatteryAlert();

    delay(5);
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
    });

    mesh.onFail([](uint32_t packetId) {
        UIManager::instance().onMessageFailed(packetId);
    });

    mesh.onAdvert([](const uint8_t* senderKey) {
        ContactStore::instance().updateLastSeen(senderKey);
    });

    mesh.onTelemetry([](const uint8_t* pubKey, const TelemetryData& data) {
        TelemetryCache::instance().store(pubKey, data);
        ContactStore::instance().updateLastSeen(pubKey);
        UIManager::instance().updateTelemetryModal(pubKey);
    });
}

static void handleKeyShortcuts() {
    auto& kb = Keyboard::instance();
    auto& ui = UIManager::instance();
    char key = kb.lastKey();
    if (key == 0) return;

    // Don't process shortcuts while PIN locked — keys go to PIN overlay via LVGL
    if (ui.isLocked()) {
        kb.clearKey();
        return;
    }

    // Don't process shortcuts while key-locked
    if (ui.isKeyLocked()) {
        kb.clearKey();
        return;
    }

    if (key == 0x1B && ui.currentScreen() != Screen::CONVO_LIST) {
        ui.goHome();
        kb.clearKey();
        return;
    }
    if (key == '0' && ui.currentScreen() == Screen::CONVO_LIST &&
        ConfigManager::instance().config().security.adminEnabled) {
        ui.showScreen(Screen::ADMIN);
        kb.clearKey();
        return;
    }
    if (ui.currentScreen() == Screen::CONVO_LIST && key >= '1' && key <= '9') {
        size_t idx = key - '1';
        auto convos = MessageStore::instance().getConversationsSorted();
        if (idx < convos.size()) {
            ui.openChat(convos[idx]->convoId);
        }
        kb.clearKey();
        return;
    }
    if (key == 0x0C && ui.currentScreen() == Screen::CHAT) {
        ui.insertLocation();
        kb.clearKey();
        return;
    }
    kb.clearKey();
}
