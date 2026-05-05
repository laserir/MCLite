#include "MeshManager.h"
#include "MCLiteMesh.h"
#include "ContactStore.h"
#include "ChannelStore.h"
#include "../config/ConfigManager.h"
#include "../hal/GPS.h"

#include <SPI.h>
#include <RadioLib.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/radiolib/CustomSX1262.h>

namespace mclite {

// ---- Hardware ----
// Use the global Arduino SPI object (already initialized by SD card on shared bus)
static CustomSX1262 radio = new Module(TDECK_LORA_CS, TDECK_LORA_DIO1,
                                        TDECK_LORA_RST, TDECK_LORA_BUSY, SPI);

static SimpleMeshTables meshTables;

MeshManager& MeshManager::instance() {
    static MeshManager inst;
    return inst;
}

bool MeshManager::initRadio() {
    const auto& cfg = ConfigManager::instance().config();

    Serial.println("[Mesh] Initializing SX1262...");

    // Use MeshCore's std_init for TCXO and -707 retry logic
    // Pass NULL to skip SPI.begin() — already initialized by SD card on shared bus
    if (!radio.std_init(NULL)) {
        Serial.println("[Mesh] Radio init failed");
        return false;
    }

    // Apply user config radio parameters (std_init uses compile-time defaults).
    // Offgrid mode swaps to the closest community band (433/869/918); other radio
    // params (SF/BW/CR/TX power) stay as configured so peers still interop with the
    // user's normal-mesh settings when they share them.
    float freq = cfg.radio.frequency;
    if (cfg.offgrid.enabled) freq = MCLiteMesh::offgridFreqFor(cfg.radio.frequency);
    radio.setFrequency(freq);
    radio.setSpreadingFactor(cfg.radio.spreadingFactor);
    radio.setBandwidth(cfg.radio.bandwidth);
    radio.setCodingRate(cfg.radio.codingRate);
    radio.setOutputPower(cfg.radio.txPower);

    Serial.println("[Mesh] SX1262 ready");
    _radioReady = true;
    return true;
}

void MeshManager::wireCallbacks() {
    if (!_mesh) return;

    // Incoming DM
    _mesh->onMessage([this](const ContactInfo& contact,
                             uint32_t timestamp, const char* text) {
        if (_onMessage) {
            _onMessage(String(contact.name), contact.id.pub_key,
                       String(text), timestamp);
        }
    });

    // Incoming group message
    _mesh->onGroupMsg([this](const mesh::GroupChannel& channel,
                              uint32_t timestamp, const char* text) {
        if (!_onGroupMsg) return;

        // Find which of our channels this message belongs to
        int meshIdx = _mesh->findChannelIdx(channel);
        if (meshIdx < 0) return;

        // Map MeshCore channel index back to our ChannelStore index
        auto& channels = ChannelStore::instance();
        if ((size_t)meshIdx >= channels.count()) return;
        const auto& allCh = channels.all();
        uint8_t channelIndex = allCh[meshIdx].index;

        // Parse "sender: message" format from MeshCore group messages
        String fullText(text);
        String senderName;
        String msgText = fullText;
        int colonPos = fullText.indexOf(": ");
        if (colonPos > 0 && colonPos < 32) {
            senderName = fullText.substring(0, colonPos);
            msgText = fullText.substring(colonPos + 2);
        }

        _onGroupMsg(channelIndex, senderName, msgText, timestamp);
    });

    // ACK received — UIManager handles MessageStore update
    _mesh->onAck([this](uint32_t packetId) {
        if (_onAck) _onAck(packetId);
    });

    // Send failed — UIManager handles MessageStore update
    _mesh->onFail([this](uint32_t packetId) {
        if (_onFail) _onFail(packetId);
    });

    // Telemetry response
    _mesh->onTelemetry([this](const ContactInfo& contact, const TelemetryData& data) {
        if (_onTelemetry) _onTelemetry(contact.id.pub_key, data);
    });

    // Advertisement received
    _mesh->onAdvert([this](const ContactInfo& contact, bool isNew) {
        // Update last-seen in our contact store
        ContactStore::instance().updateLastSeen(contact.id.pub_key);
        if (_onAdvert) _onAdvert(contact.id.pub_key);
    });

    // Helper: map a ROOM contact's pubkey back to its config index (0..7).
    // Returns -1 if not found (shouldn't happen for registered rooms).
    auto findRoomIdx = [](const ContactInfo& contact) -> int {
        const auto& rooms = ConfigManager::instance().config().roomServers;
        for (size_t i = 0; i < rooms.size(); i++) {
            // rooms[i].publicKey is 64-hex; compare against contact.id.pub_key bytes.
            if (rooms[i].publicKey.length() != 64) continue;
            bool match = true;
            for (int b = 0; b < PUB_KEY_SIZE; b++) {
                char byteStr[3] = { rooms[i].publicKey[b*2], rooms[i].publicKey[b*2+1], 0 };
                uint8_t want = (uint8_t)strtoul(byteStr, nullptr, 16);
                if (contact.id.pub_key[b] != want) { match = false; break; }
            }
            if (match) return (int)i;
        }
        return -1;
    };

    // Incoming room post (signed message from a server we're logged in to)
    _mesh->onRoomMsg([this, findRoomIdx](const ContactInfo& contact,
                                          const uint8_t* sender_prefix,
                                          uint32_t timestamp, const char* text) {
        if (!_onRoomMsg) return;
        int roomIdx = findRoomIdx(contact);
        if (roomIdx < 0) return;
        _onRoomMsg((size_t)roomIdx, String(contact.name), sender_prefix,
                   String(text), timestamp);
    });

    // Room login response (from sendLogin handshake)
    _mesh->onRoomLogin([this, findRoomIdx](const ContactInfo& contact,
                                            uint8_t status, uint8_t permissions) {
        if (!_onRoomLogin) return;
        int roomIdx = findRoomIdx(contact);
        if (roomIdx < 0) return;
        _onRoomLogin((size_t)roomIdx, String(contact.name), status, permissions);
    });
}

bool MeshManager::loginRoom(size_t roomIdx, uint32_t& estTimeout) {
    if (!_mesh) return false;
    const auto& cfg = ConfigManager::instance().config();
    if (roomIdx >= cfg.roomServers.size()) return false;
    int result = _mesh->loginRoom(roomIdx, cfg.roomServers[roomIdx].password.c_str(),
                                   estTimeout);
    return result != MSG_SEND_FAILED;
}

uint32_t MeshManager::sendRoomPost(size_t roomIdx, const String& text) {
    if (!_mesh || !_radioReady) return 0;
    const auto& cfg = ConfigManager::instance().config();
    // Same timestamp policy as sendMessage/sendGroupMessage: GPS epoch when
    // synced, else millis()/1000 as a unique-per-message fallback.
    uint32_t timestamp = GPS::instance().isTimeSynced()
        ? GPS::instance().currentTimestamp() : (millis() / 1000);
    return _mesh->sendRoomPost(roomIdx, text.c_str(), timestamp,
                                cfg.messaging.maxRetries);
}

bool MeshManager::init() {
    // Load contacts and channels first
    ContactStore::instance().loadFromConfig();
    ChannelStore::instance().loadFromConfig();

    if (!initRadio()) {
        Serial.println("[Mesh] Radio failed, running in offline mode");
        return false;
    }

    // Create and initialize MCLiteMesh
    _mesh = new MCLiteMesh(radio, meshTables);
    wireCallbacks();

    const auto& cfg = ConfigManager::instance().config();
    float activeFreq = cfg.offgrid.enabled
        ? MCLiteMesh::offgridFreqFor(cfg.radio.frequency)
        : cfg.radio.frequency;
    _mesh->setFrequency(activeFreq);

    if (!_mesh->begin(cfg.deviceName.c_str())) {
        Serial.println("[Mesh] Mesh begin failed");
        delete _mesh;
        _mesh = nullptr;
        return false;
    }

    Serial.println("[Mesh] Initialization complete");
    return true;
}

void MeshManager::update() {
    if (!_mesh || !_radioReady) return;

    // Process radio I/O, incoming packets, ACK timeouts
    _mesh->loop();

    // Periodic advertisement (first one fires immediately on boot)
    if (_advertIntervalMs > 0) {
        uint32_t now = millis();
        if (_firstAdvert || now - _lastAdvertMs >= _advertIntervalMs) {
            _firstAdvert = false;
            const auto& cfg = ConfigManager::instance().config();
            _mesh->advertise(cfg.deviceName.c_str());
            _lastAdvertMs = now;
        }
    }

    // Sample TX airtime every 60s for rolling duty cycle calculation
    {
        uint32_t now = millis();
        if (now - _dcLastSampleMs >= 60000) {
            uint32_t currentAirtime = _mesh->getTotalAirTime();
            if (_dcLastSampleMs > 0) {
                _dcDeltas[_dcSlotIdx] = currentAirtime - _dcLastSample;
                _dcSlotIdx = (_dcSlotIdx + 1) % DC_SLOTS;
                if (_dcSlotsFilled < DC_SLOTS) _dcSlotsFilled++;
            }
            _dcLastSample = currentAirtime;
            _dcLastSampleMs = now;
        }
    }
}

uint32_t MeshManager::sendMessage(size_t contactIndex, const String& text) {
    if (!_mesh || !_radioReady) return 0;

    const auto& cfg = ConfigManager::instance().config();
    // Use GPS epoch time if available, otherwise millis()/1000 as unique fallback
    // (wrong date but prevents packet dedup when sending same text twice)
    uint32_t timestamp = GPS::instance().isTimeSynced()
        ? GPS::instance().currentTimestamp() : (millis() / 1000);

    return _mesh->sendDM(contactIndex, text.c_str(), timestamp,
                          cfg.messaging.maxRetries);
}

uint32_t MeshManager::sendGroupMessage(uint8_t channelIndex, const String& text) {
    if (!_mesh || !_radioReady) return 0;

    const auto& cfg = ConfigManager::instance().config();
    uint32_t timestamp = GPS::instance().isTimeSynced()
        ? GPS::instance().currentTimestamp() : (millis() / 1000);

    // Find the MeshCore channel index (our ChannelStore index maps to
    // the order we called addChannel during init)
    Channel* ch = ChannelStore::instance().findByIndex(channelIndex);
    if (!ch) return 0;

    // Find position in our channel list
    int meshIdx = -1;
    const auto& allCh = ChannelStore::instance().all();
    for (size_t i = 0; i < allCh.size(); i++) {
        if (allCh[i].index == channelIndex) {
            meshIdx = (int)i;
            break;
        }
    }
    if (meshIdx < 0) return 0;

    bool ok = _mesh->sendGroup(meshIdx, cfg.deviceName.c_str(),
                                text.c_str(), timestamp);
    if (!ok) return 0;

    // Group messages are fire-and-forget — return a nonzero ID for the caller
    // to create a message with SENT status
    static uint32_t groupMsgId = 0x80000000;  // High bit set to distinguish from DM IDs
    uint32_t id = ++groupMsgId;
    if (id == 0) id = ++groupMsgId;
    return id;
}

bool MeshManager::requestTelemetry(size_t contactIndex, uint32_t& estTimeout) {
    if (!_mesh || !_radioReady) return false;
    return _mesh->requestTelemetry(contactIndex, estTimeout);
}

void MeshManager::clearPendingTelemetry() {
    if (_mesh) _mesh->clearPendingTelemetry();
}

bool MeshManager::sendAdvertNow() {
    if (!_mesh || !_radioReady) return false;
    const auto& cfg = ConfigManager::instance().config();
    bool ok = _mesh->advertise(cfg.deviceName.c_str());
    if (ok) {
        // Re-anchor the periodic schedule so the next automatic advert
        // is a full interval after this manual one.
        _lastAdvertMs = millis();
        _firstAdvert  = false;
    }
    return ok;
}

float MeshManager::getTxDutyCyclePercent() const {
    if (_dcSlotsFilled == 0) return 0.0f;
    uint32_t totalMs = 0;
    for (uint8_t i = 0; i < _dcSlotsFilled; i++) {
        totalMs += _dcDeltas[i];
    }
    float windowMs = (float)_dcSlotsFilled * 60000.0f;
    return (totalMs / windowMs) * 100.0f;
}

bool MeshManager::isEURegion() const {
    float freq = ConfigManager::instance().config().radio.frequency;
    return freq >= 868.0f && freq <= 870.0f;
}

}  // namespace mclite
