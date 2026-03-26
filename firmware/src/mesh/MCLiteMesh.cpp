#include "MCLiteMesh.h"
#include "ContactStore.h"
#include "ChannelStore.h"
#include "../config/ConfigManager.h"
#include "../hal/Battery.h"
#include "../hal/GPS.h"
#include <helpers/ArduinoHelpers.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <mbedtls/base64.h>
#include <cstring>

namespace mclite {

// Minimal MainBoard for MCLite (we handle battery/display ourselves)
class MCLiteBoard : public ESP32Board {
public:
    const char* getManufacturerName() const override { return "MCLite"; }
};

// Persistent helper objects (live for entire program)
static ArduinoMillis sMillis;
static StdRNG sRng;
static ESP32RTCClock sRtcClock;
static StaticPoolPacketManager sPktMgr(PACKET_POOL_SIZE);
static MCLiteBoard sBoard;

MCLiteMesh::MCLiteMesh(CustomSX1262& radio, SimpleMeshTables& tables)
    : BaseChatMesh(
          *(new CustomSX1262Wrapper(radio, sBoard)),
          sMillis, sRng, sRtcClock, sPktMgr, tables),
      _telemetry(32)
{
    memset(_acks, 0, sizeof(_acks));
}

MCLiteMesh::~MCLiteMesh() {
    // Intentionally leak — these objects live for the entire program
}

bool MCLiteMesh::begin(const char* deviceName) {
    const auto& cfg = ConfigManager::instance().config();

    // 1. Load or generate identity
    if (cfg.privateKey.length() > 0 && cfg.publicKey.length() > 0) {
        bool loaded = false;

        if (cfg.privateKey.length() == PRV_KEY_SIZE * 2 &&
            cfg.publicKey.length() == PUB_KEY_SIZE * 2) {
            // Hex format — pass directly to LocalIdentity
            self_id = mesh::LocalIdentity(cfg.privateKey.c_str(), cfg.publicKey.c_str());
            loaded = true;
        } else {
            // Base64 format (legacy) — decode then convert to hex
            uint8_t privKey[PRV_KEY_SIZE], pubKey[PUB_KEY_SIZE];
            size_t privLen = 0, pubLen = 0;
            int r1 = mbedtls_base64_decode(privKey, sizeof(privKey), &privLen,
                                           (const uint8_t*)cfg.privateKey.c_str(),
                                           cfg.privateKey.length());
            int r2 = mbedtls_base64_decode(pubKey, sizeof(pubKey), &pubLen,
                                           (const uint8_t*)cfg.publicKey.c_str(),
                                           cfg.publicKey.length());
            if (r1 == 0 && r2 == 0 && privLen == PRV_KEY_SIZE && pubLen == PUB_KEY_SIZE) {
                char privHex[PRV_KEY_SIZE * 2 + 1], pubHex[PUB_KEY_SIZE * 2 + 1];
                for (size_t i = 0; i < PRV_KEY_SIZE; i++) sprintf(privHex + i*2, "%02x", privKey[i]);
                for (size_t i = 0; i < PUB_KEY_SIZE; i++) sprintf(pubHex + i*2, "%02x", pubKey[i]);
                privHex[PRV_KEY_SIZE * 2] = '\0';
                pubHex[PUB_KEY_SIZE * 2] = '\0';
                self_id = mesh::LocalIdentity(privHex, pubHex);
                loaded = true;
            }
        }

        if (loaded) {
            Serial.println("[MCLiteMesh] Identity loaded from config");
        } else {
            Serial.println("[MCLiteMesh] Invalid identity in config, generating new");
            self_id = mesh::LocalIdentity(getRNG());
            _saveIdentity();
        }
    } else {
        self_id = mesh::LocalIdentity(getRNG());
        _saveIdentity();
        Serial.println("[MCLiteMesh] New identity generated");
    }

    // 2. Register contacts from ContactStore
    auto& contacts = ContactStore::instance();
    for (size_t i = 0; i < contacts.count(); i++) {
        const Contact* c = contacts.findByIndex(i);
        if (!c) continue;

        ContactInfo ci;
        memset(&ci, 0, sizeof(ci));
        memcpy(ci.id.pub_key, c->publicKey, PUB_KEY_SIZE);
        strncpy(ci.name, c->name.c_str(), sizeof(ci.name) - 1);
        ci.type = ADV_TYPE_CHAT;
        ci.out_path_len = OUT_PATH_UNKNOWN;
        ci.shared_secret_valid = false;

        addContact(ci);
    }
    Serial.printf("[MCLiteMesh] Registered %d contacts\n", getNumContacts());

    // 3. Register channels from ChannelStore
    auto& channels = ChannelStore::instance();
    for (const auto& ch : channels.all()) {
        addChannel(ch.name.c_str(),  ch.pskB64.c_str());
    }
    Serial.printf("[MCLiteMesh] Registered %d channels\n", (int)channels.count());

    // 4. Start the mesh
    Mesh::begin();

    _ready = true;
    Serial.println("[MCLiteMesh] Mesh started");
    return true;
}

void MCLiteMesh::_saveIdentity() {
    auto& cfgMut = ConfigManager::instance().config();

    // Save private key as hex
    uint8_t privBuf[PRV_KEY_SIZE];
    size_t written = self_id.writeTo(privBuf, PRV_KEY_SIZE);
    constexpr size_t MAX_KEY = PRV_KEY_SIZE > PUB_KEY_SIZE ? PRV_KEY_SIZE : PUB_KEY_SIZE;
    char hex[MAX_KEY * 2 + 1];
    for (size_t i = 0; i < written; i++) sprintf(hex + i*2, "%02x", privBuf[i]);
    hex[written * 2] = '\0';
    cfgMut.privateKey = String(hex);

    // Save public key as hex
    for (size_t i = 0; i < PUB_KEY_SIZE; i++) sprintf(hex + i*2, "%02x", self_id.pub_key[i]);
    hex[PUB_KEY_SIZE * 2] = '\0';
    cfgMut.publicKey = String(hex);

    ConfigManager::instance().save();
}

void MCLiteMesh::loop() {
    if (!_ready) return;

    // Process radio I/O, dispatch incoming packets
    BaseChatMesh::loop();

    // Check for timed-out ACKs
    checkAckTimeouts();
}

bool MCLiteMesh::advertise(const char* name) {
    if (!_ready) return false;

    // Create advertisement without GPS coordinates
    // Location is only shared via telemetry response to authorized contacts
    mesh::Packet* pkt = createSelfAdvert(name);
    if (!pkt) return false;

    sendFlood(pkt);
    Serial.printf("[MCLiteMesh] Advertised as %s\n", name);
    return true;
}

uint32_t MCLiteMesh::sendDM(size_t contactIdx, const char* text, uint32_t timestamp,
                             uint8_t maxRetries) {
    if (!_ready) return 0;

    ContactInfo* ci = getContactByIdx((int)contactIdx);
    if (!ci) {
        Serial.println("[MCLiteMesh] Invalid contact index for DM");
        return 0;
    }

    uint32_t packetId = allocPacketId();
    uint32_t expectedAck = 0;
    uint32_t estTimeout = 0;

    int result = sendMessage(*ci, timestamp, 1, text, expectedAck, estTimeout);

    if (result == MSG_SEND_FAILED) {
        Serial.printf("[MCLiteMesh] DM send failed to %s\n", ci->name);
        return 0;
    }

    Serial.printf("[MCLiteMesh] DM sent to %s (packetId=%u, %s, timeout=%ums)\n",
                  ci->name, packetId,
                  result == MSG_SEND_SENT_DIRECT ? "direct" : "flood",
                  estTimeout);

    // Track ACK
    AckEntry* entry = findFreeAck();
    if (entry) {
        entry->expectedAck = expectedAck;
        entry->packetId    = packetId;
        entry->timeoutMs   = millis() + estTimeout;
        entry->attempt     = 1;
        entry->maxAttempts = maxRetries;
        entry->contactIdx  = contactIdx;
        entry->timestamp   = timestamp;
        strncpy(entry->text, text, sizeof(entry->text) - 1);
        entry->text[sizeof(entry->text) - 1] = '\0';
        entry->active      = true;
    }

    return packetId;
}

bool MCLiteMesh::sendGroup(int channelIdx, const char* senderName, const char* text,
                            uint32_t timestamp) {
    if (!_ready) return false;

    ChannelDetails cd;
    if (!getChannel(channelIdx, cd)) {
        Serial.printf("[MCLiteMesh] Invalid channel index %d\n", channelIdx);
        return false;
    }

    bool ok = sendGroupMessage(timestamp, cd.channel, senderName, text, strlen(text));
    if (ok) {
        Serial.printf("[MCLiteMesh] Group msg sent to %s\n", cd.name);
    }
    return ok;
}

ContactInfo* MCLiteMesh::getContactByIdx(int idx) {
    // Use BaseChatMesh's contact iterator
    ContactInfo ci;
    if (BaseChatMesh::getContactByIdx((uint32_t)idx, ci)) {
        // Return pointer via lookup
        return lookupContactByPubKey(ci.id.pub_key, PUB_KEY_SIZE);
    }
    return nullptr;
}


// ---- ACK tracking ----

AckEntry* MCLiteMesh::findFreeAck() {
    for (int i = 0; i < MAX_PENDING_ACKS; i++) {
        if (!_acks[i].active) return &_acks[i];
    }
    // Evict oldest (earliest timeout) if full
    int oldest = 0;
    for (int i = 1; i < MAX_PENDING_ACKS; i++) {
        if ((int32_t)(_acks[i].timeoutMs - _acks[oldest].timeoutMs) < 0) {
            oldest = i;
        }
    }
    if (_onFail) _onFail(_acks[oldest].packetId);
    _acks[oldest].active = false;
    return &_acks[oldest];
}

AckEntry* MCLiteMesh::findAckByHash(uint32_t hash) {
    for (int i = 0; i < MAX_PENDING_ACKS; i++) {
        if (_acks[i].active && _acks[i].expectedAck == hash) return &_acks[i];
    }
    return nullptr;
}

void MCLiteMesh::checkAckTimeouts() {
    uint32_t now = millis();
    for (int i = 0; i < MAX_PENDING_ACKS; i++) {
        if (!_acks[i].active) continue;
        if ((int32_t)(now - _acks[i].timeoutMs) < 0) continue;

        // If outbound queue still has packets, our packet may not have
        // transmitted yet — extend timeout instead of retrying
        // (retrying would change expectedAck, orphaning the real ACK)
        if (_mgr->getOutboundCount(_ms->getMillis()) > 0) {
            _acks[i].timeoutMs = now + 2000;
            continue;
        }
        retryOrFail(_acks[i]);
    }
}

void MCLiteMesh::retryOrFail(AckEntry& entry) {
    if (entry.attempt < entry.maxAttempts) {
        entry.attempt++;
        Serial.printf("[MCLiteMesh] Retrying packet %u (attempt %d/%d)\n",
                      entry.packetId, entry.attempt, entry.maxAttempts);

        ContactInfo* ci = getContactByIdx((int)entry.contactIdx);
        if (ci) {
            uint32_t newAck = 0, newTimeout = 0;
            int result = sendMessage(*ci, entry.timestamp, entry.attempt,
                                     entry.text, newAck, newTimeout);
            if (result != MSG_SEND_FAILED) {
                entry.expectedAck = newAck;
                entry.timeoutMs   = millis() + newTimeout;
                return;
            }
        }
        // Retry send failed — fall through to fail
    }

    // All retries exhausted or retry send failed
    Serial.printf("[MCLiteMesh] Packet %u FAILED after %d attempts\n",
                  entry.packetId, entry.attempt);
    uint32_t pid = entry.packetId;
    entry.active = false;
    if (_onFail) _onFail(pid);
}

// ---- BaseChatMesh overrides ----

void MCLiteMesh::onDiscoveredContact(ContactInfo& contact, bool is_new,
                                      uint8_t path_len, const uint8_t* path) {
    Serial.printf("[MCLiteMesh] Discovered contact: %s (%s, hops=%d)\n",
                  contact.name, is_new ? "new" : "update", path_len);

    if (_onAdvert) _onAdvert(contact, is_new);
}

ContactInfo* MCLiteMesh::processAck(const uint8_t* data) {
    uint32_t ackHash;
    memcpy(&ackHash, data, 4);

    AckEntry* entry = findAckByHash(ackHash);
    if (entry) {
        uint32_t pid = entry->packetId;
        size_t cidx = entry->contactIdx;
        entry->active = false;

        Serial.printf("[MCLiteMesh] ACK received for packet %u\n", pid);

        if (_onAck) _onAck(pid);

        // Return the contact for MeshCore's internal bookkeeping
        return getContactByIdx((int)cidx);
    }

    return nullptr;
}

void MCLiteMesh::onContactPathUpdated(const ContactInfo& contact) {
    Serial.printf("[MCLiteMesh] Path updated for %s (hops=%d)\n",
                  contact.name, contact.out_path_len);
}

void MCLiteMesh::onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt,
                                uint32_t sender_timestamp, const char* text) {
    Serial.printf("[MCLiteMesh] DM from %s: %s\n", contact.name, text);
    if (_onMessage) _onMessage(contact, sender_timestamp, text);
}

void MCLiteMesh::onCommandDataRecv(const ContactInfo& contact, mesh::Packet* pkt,
                                    uint32_t sender_timestamp, const char* text) {
    // CLI commands not used in MCLite — ignore
    Serial.printf("[MCLiteMesh] CLI data from %s (ignored)\n", contact.name);
}

void MCLiteMesh::onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt,
                                      uint32_t sender_timestamp,
                                      const uint8_t* sender_prefix, const char* text) {
    // Signed messages (room server) — treat as regular messages for display
    Serial.printf("[MCLiteMesh] Signed msg from %s: %s\n", contact.name, text);
    if (_onMessage) _onMessage(contact, sender_timestamp, text);
}

void MCLiteMesh::onChannelMessageRecv(const mesh::GroupChannel& channel,
                                       mesh::Packet* pkt,
                                       uint32_t timestamp, const char* text) {
    Serial.printf("[MCLiteMesh] Group msg on channel: %s\n", text);
    if (_onGroupMsg) _onGroupMsg(channel, timestamp, text);
}

uint32_t MCLiteMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
    // 30x airtime for mesh flood propagation + queue delay margin
    // (accounts for packet waiting behind airtime budget silence)
    uint32_t base = pkt_airtime_millis * 30;
    uint32_t queueMargin = (uint32_t)(pkt_airtime_millis * (1.0f + getAirtimeBudgetFactor()));
    return base + queueMargin;
}

uint32_t MCLiteMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis,
                                                  uint8_t path_len) const {
    // 5x airtime per hop (round trip) + queue delay margin
    uint32_t hops = path_len > 0 ? path_len : 1;
    uint32_t base = pkt_airtime_millis * 5 * hops;
    uint32_t queueMargin = (uint32_t)(pkt_airtime_millis * (1.0f + getAirtimeBudgetFactor()));
    return base + queueMargin;
}

void MCLiteMesh::onSendTimeout() {
    // BaseChatMesh handles the internal timeout — our checkAckTimeouts() handles ours
    Serial.println("[MCLiteMesh] Internal send timeout");
}

uint8_t MCLiteMesh::onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp,
                                      const uint8_t* data, uint8_t len,
                                      uint8_t* reply) {
    if (len == 0) return 0;

    // Only handle MeshCore-standard telemetry requests (0x03)
    if (data[0] != REQ_TYPE_GET_TELEMETRY_DATA) return 0;

    // Find contact to check permissions
    Contact* ourContact = ContactStore::instance().findByPublicKey(contact.id.pub_key);
    if (!ourContact) {
        Serial.printf("[MCLiteMesh] Telemetry request from unknown contact %s — ignored\n", contact.name);
        return 0;
    }

    // They just talked to us — mark them as seen
    ContactStore::instance().updateLastSeen(contact.id.pub_key);

    // Build permission bitmask from per-contact settings
    uint8_t permissions = 0;
    if (ourContact->allowTelemetry)   permissions |= TELEM_PERM_BASE;
    if (ourContact->allowLocation)    permissions |= TELEM_PERM_LOCATION;
    if (ourContact->allowEnvironment) permissions |= TELEM_PERM_ENVIRONMENT;

    // Apply requester's inverse permission mask (data[1] if present)
    if (len > 1) {
        permissions &= ~data[1];
    }

    // MeshCore convention: no response unless BASE is permitted
    if (!(permissions & TELEM_PERM_BASE)) {
        Serial.printf("[MCLiteMesh] Telemetry request from %s denied (no base permission)\n", contact.name);
        return 0;
    }

    // Build CayenneLPP response
    _telemetry.reset();

    // Battery voltage (always included when BASE is set)
    auto& batt = Battery::instance();
    batt.update();
    _telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)batt.milliVolts() / 1000.0f);

    // GPS location (if permitted and fix available — includes cached position)
    if (permissions & TELEM_PERM_LOCATION) {
        auto& gps = GPS::instance();
        FixStatus status = gps.fixStatus();
        if (status == FixStatus::LIVE) {
            _telemetry.addGPS(TELEM_CHANNEL_SELF,
                              (float)gps.lat(), (float)gps.lon(), (float)gps.altitude());
        } else if (status == FixStatus::LAST_KNOWN) {
            auto& pos = gps.lastPosition();
            _telemetry.addGPS(TELEM_CHANNEL_SELF,
                              (float)pos.lat, (float)pos.lon, (float)pos.altitude);
        }
    }

    // Environment: T-Deck Plus has no environment sensors, nothing to add
    // (permission bit is handled correctly — just no data to send)

    // Pack response: [4-byte reflected timestamp][CayenneLPP data]
    memcpy(reply, &sender_timestamp, 4);
    uint8_t tlen = _telemetry.getSize();
    memcpy(reply + 4, _telemetry.getBuffer(), tlen);

    Serial.printf("[MCLiteMesh] Telemetry response to %s: %d bytes LPP\n", contact.name, tlen);
    return 4 + tlen;
}

bool MCLiteMesh::requestTelemetry(size_t contactIdx, uint32_t& estTimeout) {
    if (!_ready) return false;

    ContactInfo* ci = getContactByIdx((int)contactIdx);
    if (!ci) {
        Serial.println("[MCLiteMesh] Invalid contact index for telemetry request");
        return false;
    }

    uint32_t tag = 0;
    int result = sendRequest(*ci, REQ_TYPE_GET_TELEMETRY_DATA, tag, estTimeout);
    if (result == MSG_SEND_FAILED) {
        Serial.printf("[MCLiteMesh] Telemetry request failed to %s\n", ci->name);
        return false;
    }

    _pendingTelemTag = tag;
    memcpy(_pendingTelemKey, ci->id.pub_key, PUB_KEY_SIZE);
    Serial.printf("[MCLiteMesh] Telemetry requested from %s (timeout=%ums)\n",
                  ci->name, estTimeout);
    return true;
}

// CayenneLPP type sizes (for skipping unknown/unhandled types)
static int lppTypeSize(uint8_t type) {
    switch (type) {
        case 0:   return 1;  // Digital input
        case 1:   return 1;  // Digital output
        case 2:   return 2;  // Analog input
        case 3:   return 2;  // Analog output
        case 100: return 4;  // Generic 4-byte
        case 101: return 2;  // Illuminance
        case 102: return 1;  // Presence
        case 103: return 2;  // Temperature
        case 104: return 1;  // Humidity
        case 113: return 6;  // Accelerometer
        case 114: return 2;  // Load (weight)
        case 115: return 2;  // Barometer
        case 116: return 2;  // Voltage
        case 117: return 2;  // Current
        case 118: return 4;  // Frequency
        case 120: return 1;  // Percentage
        case 121: return 2;  // Altitude
        case 122: return 2;  // Concentration
        case 125: return 1;  // Loudness (dB)
        case 128: return 2;  // Power
        case 130: return 4;  // Distance
        case 131: return 4;  // Energy
        case 133: return 3;  // Colour
        case 134: return 2;  // Direction
        case 136: return 9;  // GPS
        case 137: return 6;  // Gyrometer
        case 142: return 1;  // Switch
        case 188: return 2;  // Timestamp (partial)
        default:
            Serial.printf("[MCLiteMesh] Unknown LPP type %d — skipping rest of payload\n", type);
            return -1;
    }
}

void MCLiteMesh::onContactResponse(const ContactInfo& contact,
                                    const uint8_t* data, uint8_t len) {
    Serial.printf("[MCLiteMesh] Response from %s (%d bytes)\n", contact.name, len);

    // Need at least 4 bytes (reflected timestamp) + some payload
    if (len <= 4) return;

    // Validate: must have a pending request, from the expected contact
    if (_pendingTelemTag == 0) {
        Serial.println("[MCLiteMesh] Unexpected response (no pending request)");
        return;
    }
    if (memcmp(contact.id.pub_key, _pendingTelemKey, PUB_KEY_SIZE) != 0) {
        Serial.printf("[MCLiteMesh] Response from wrong contact %s — ignored\n", contact.name);
        return;
    }

    // Validate reflected tag matches
    uint32_t reflectedTag;
    memcpy(&reflectedTag, data, 4);
    if (reflectedTag != _pendingTelemTag) {
        Serial.println("[MCLiteMesh] Response tag mismatch — ignored");
        return;
    }

    // Parse CayenneLPP from data+4
    const uint8_t* lpp = data + 4;
    uint8_t lppLen = len - 4;

    TelemetryData telem;
    telem.receivedAt = millis();

    uint8_t pos = 0;
    while (pos + 2 < lppLen) {
        // uint8_t channel = lpp[pos]; // not needed
        uint8_t type = lpp[pos + 1];
        pos += 2;

        int dataSize = lppTypeSize(type);
        if (dataSize < 0 || pos + dataSize > lppLen) break;

        if (type == 103 && dataSize == 2) {
            // Temperature: 2 bytes signed, /10.0 = °C
            int16_t raw = ((int16_t)(int8_t)lpp[pos] << 8) | lpp[pos + 1];
            telem.temperature = raw / 10.0f;
            telem.hasTemperature = true;
        } else if (type == 104 && dataSize == 1) {
            // Humidity: 1 byte, /2.0 = %
            telem.humidity = lpp[pos] / 2.0f;
            telem.hasHumidity = true;
        } else if (type == 115 && dataSize == 2) {
            // Barometer: 2 bytes unsigned, /10.0 = hPa
            uint16_t raw = ((uint16_t)lpp[pos] << 8) | lpp[pos + 1];
            telem.pressure = raw / 10.0f;
            telem.hasPressure = true;
        } else if (type == 116 && dataSize == 2) {
            // Voltage: 2 bytes unsigned, /100.0
            uint16_t raw = ((uint16_t)lpp[pos] << 8) | lpp[pos + 1];
            telem.voltage = raw / 100.0f;
            telem.hasVoltage = true;
        } else if (type == 136 && dataSize == 9) {
            // GPS: lat(3B signed)/10000, lon(3B signed)/10000, alt(3B signed)/100
            int32_t latRaw = ((int32_t)(int8_t)lpp[pos] << 16) |
                             ((int32_t)lpp[pos + 1] << 8) | lpp[pos + 2];
            int32_t lonRaw = ((int32_t)(int8_t)lpp[pos + 3] << 16) |
                             ((int32_t)lpp[pos + 4] << 8) | lpp[pos + 5];
            int32_t altRaw = ((int32_t)(int8_t)lpp[pos + 6] << 16) |
                             ((int32_t)lpp[pos + 7] << 8) | lpp[pos + 8];
            telem.lat = latRaw / 10000.0;
            telem.lon = lonRaw / 10000.0;
            telem.alt = altRaw / 100.0;
            telem.hasLocation = true;
        }

        pos += dataSize;
    }

    _pendingTelemTag = 0;
    memset(_pendingTelemKey, 0, PUB_KEY_SIZE);

    if (_onTelemetry) _onTelemetry(contact, telem);
}

float MCLiteMesh::getAirtimeBudgetFactor() const {
    // EU 868-870 MHz (ETSI EN 300 220, G3 sub-band): 10% duty cycle → factor 9.0
    // All other frequencies: MeshCore default 33% → factor 2.0
    if (_frequency >= 868.0f && _frequency <= 870.0f) return 9.0f;
    return 2.0f;
}

}  // namespace mclite
