#include "MCLiteMesh.h"
#include "util/log.h"
#include "ContactStore.h"
#include "ChannelStore.h"
#include "../config/ConfigManager.h"
#include "../hal/Battery.h"
#include "../hal/GPS.h"
#include "../storage/HeardAdvertCache.h"
#include "../storage/MessageStore.h"
#include "../storage/SDCard.h"
#include "../util/hex.h"
#include "../util/offgrid.h"
#include "../util/locprecision.h"
#include <helpers/AdvertDataHelpers.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
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

// FNV-1a 32-bit hash.
static uint32_t fnv1a32(const uint8_t* d, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
    return h ? h : 1;   // 0 is our "empty" sentinel
}
static bool isMsgFloodType(uint8_t ptype) {
    return ptype == PAYLOAD_TYPE_TXT_MSG || ptype == PAYLOAD_TYPE_GRP_TXT;
}

// Derive a TransportKey from a scope string ("*"/empty → null key, "#name" → SHA256)
static TransportKey scopeToTransportKey(const String& scope) {
    TransportKey tk;
    memset(tk.key, 0, sizeof(tk.key));
    if (scope == "*" || scope.length() == 0) return tk;
    // Match MeshCore: a bare region name is an implicit hashtag region, so prepend '#' before
    // hashing — RegionMap feeds getAutoKeyFor the '#'-prefixed name, so SHA256("#region")[:16]
    // is the canonical key. An explicit '#' (public hashtag) passes through and interoperates.
    // NOTE: a '$' (private) prefix also passes through to SHA256("$name"), but real MeshCore
    // private regions use a SEPARATELY STORED key (RegionMap::loadKeysFor), NOT a name hash —
    // so a '$' scope is an MCLite-only convention that scopes MCLite<->MCLite traffic but does
    // NOT interoperate with a MeshCore private region. The app path (CMD_SET_DEFAULT_FLOOD_SCOPE,
    // 63) already rejects such mismatches (BAD_STATE); see companion-commands.md.
    String name = (scope[0] == '#' || scope[0] == '$') ? scope : (String("#") + scope);
    uint8_t hash[32];
    mbedtls_sha256((const uint8_t*)name.c_str(), name.length(), hash, 0);
    memcpy(tk.key, hash, 16);
    return tk;
}

void MCLiteMesh::deriveScopeKey(const String& scope, uint8_t out[16]) {
    TransportKey tk = scopeToTransportKey(scope);
    memcpy(out, tk.key, 16);
}

void MCLiteMesh::setGlobalScope(const uint8_t key[16]) {
    memcpy(_globalScope.key, key, 16);   // live session override (not persisted)
}

uint8_t MCLiteMesh::exportSelf(const char* name, uint8_t out[]) {
    mesh::Packet* pkt = createSelfAdvert(name);
    if (!pkt) return 0;
    pkt->header |= ROUTE_TYPE_FLOOD;     // would be sent flood; matches the reference export
    uint8_t n = pkt->writeTo(out);
    releasePacket(pkt);                  // undo obtainNewPacket() inside createSelfAdvert
    return n;
}

uint8_t MCLiteMesh::exportContactBlob(const uint8_t* pubKey, uint8_t out[]) {
    int n = getBlobByKey(pubKey, PUB_KEY_SIZE, out);
    return (n > 0) ? (uint8_t)n : 0;
}

MCLiteMesh::MCLiteMesh(CustomSX1262& radio, SimpleMeshTables& tables)
    : BaseChatMesh(
          *(new CustomSX1262Wrapper(radio, sBoard)),
          sMillis, sRng, sRtcClock, sPktMgr, tables),
      _telemetry(32)
{
    memset(_acks, 0, sizeof(_acks));
    memset(_globalScope.key, 0, sizeof(_globalScope.key));
}

MCLiteMesh::~MCLiteMesh() {
    // Intentionally leak — these objects live for the entire program
}

float MCLiteMesh::offgridFreqFor(float f) {
    return mclite::offgridFreqFor(f);
}

bool MCLiteMesh::begin(const char* deviceName) {
    const auto& cfg = ConfigManager::instance().config();
    _offgridEnabled = cfg.offgrid.enabled;
    if (_offgridEnabled) {
        LOGF("[MCLiteMesh] OFFGRID mode active — forwarding packets on %.3f MHz\n", _frequency);
    }

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
            LOGLN("[MCLiteMesh] Identity loaded from config");
        } else {
            LOGLN("[MCLiteMesh] Invalid identity in config, generating new");
            self_id = mesh::LocalIdentity(getRNG());
            _saveIdentity();
        }
    } else {
        self_id = mesh::LocalIdentity(getRNG());
        _saveIdentity();
        LOGLN("[MCLiteMesh] New identity generated");
    }

    // 2. Register contacts from ContactStore (cap at 32 chat contacts; rooms get
    // the remaining 8 slots in the shared MeshCore contacts[MAX_CONTACTS] array.)
    auto& contacts = ContactStore::instance();
    constexpr size_t CHAT_CAP = 32;
    if (contacts.count() > CHAT_CAP) {
        LOGF("[MCLiteMesh] WARN: ignoring %u contact(s) beyond 32-cap\n",
                      (unsigned)(contacts.count() - CHAT_CAP));
    }
    size_t chatToRegister = contacts.count() < CHAT_CAP ? contacts.count() : CHAT_CAP;
    for (size_t i = 0; i < chatToRegister; i++) {
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
    LOGF("[MCLiteMesh] Registered %d chat contact(s)\n", getNumContacts());

    // 2b. Register room servers (up to 8). Pubkey decoded from 64-hex; sync_since
    // seeded from the per-room history file so server-side push only replays new
    // posts. Cached BaseChatMesh contact-index in _roomContactIdx[] for O(1)
    // lookup in loginRoom() / sendRoomPost().
    constexpr size_t ROOM_CAP = MAX_ROOMS_RUNTIME;  // 8
    size_t roomCount = cfg.roomServers.size();
    if (roomCount > ROOM_CAP) {
        LOGF("[MCLiteMesh] WARN: ignoring %u room(s) beyond 8-cap\n",
                      (unsigned)(roomCount - ROOM_CAP));
        roomCount = ROOM_CAP;
    }
    int registeredRooms = 0;
    for (size_t i = 0; i < roomCount; i++) {
        const auto& rs = cfg.roomServers[i];

        // Decode 64-hex pubkey into 32 raw bytes
        if (rs.publicKey.length() != 64 || !isHexString(rs.publicKey)) {
            LOGF("[MCLiteMesh] Skipping room '%s': invalid pubkey\n",
                          rs.name.c_str());
            continue;
        }
        uint8_t pub[PUB_KEY_SIZE];
        for (int b = 0; b < PUB_KEY_SIZE; b++) {
            char byteStr[3] = { rs.publicKey[b*2], rs.publicKey[b*2+1], 0 };
            pub[b] = (uint8_t)strtoul(byteStr, nullptr, 16);
        }

        // Detect pubkey collision with an already-registered chat contact.
        // BaseChatMesh routes inbound packets by pubkey lookup (first match wins),
        // so a duplicate would shadow the room contact and signed messages would
        // arrive on the chat dispatch path instead — silently breaking the room.
        if (lookupContactByPubKey(pub, PUB_KEY_SIZE) != nullptr) {
            LOGF("[MCLiteMesh] Skipping room '%s': pubkey collides with "
                          "an existing chat contact\n", rs.name.c_str());
            continue;
        }

        // Compute 8-byte shortId hex (16 chars) for the per-room history file
        char shortId[17];
        for (int s = 0; s < 8; s++) sprintf(shortId + s*2, "%02x", pub[s]);
        shortId[16] = '\0';

        // Seed sync_since from MessageStore (loads /mclite/history/room_<id>.json).
        // readOnly is honored so the input bar hides on listen-only rooms.
        ConvoId rid { ConvoId::ROOM, String(shortId) };
        auto& store = MessageStore::instance();
        store.ensureConversation(rid, rs.name, /*isPrivate=*/false, rs.readOnly);
        store.loadHistory(rid);
        uint32_t syncSince = 0;
        if (Conversation* convo = store.getConversation(rid)) {
            syncSince = convo->syncSince;
        }

        // Parse per-room scope override (null TransportKey = inherit _globalScope)
        if (rs.scope.length() > 0) {
            _roomScope[i] = scopeToTransportKey(rs.scope);
            if (!_roomScope[i].isNull()) {
                LOGF("[MCLiteMesh] Room '%s' scope: %s\n",
                              rs.name.c_str(), rs.scope.c_str());
            }
        }

        ContactInfo ci;
        memset(&ci, 0, sizeof(ci));
        memcpy(ci.id.pub_key, pub, PUB_KEY_SIZE);
        strncpy(ci.name, rs.name.c_str(), sizeof(ci.name) - 1);
        ci.type = ADV_TYPE_ROOM;
        ci.out_path_len = OUT_PATH_UNKNOWN;
        ci.shared_secret_valid = false;
        ci.sync_since = syncSince;

        if (!addContact(ci)) {
            LOGF("[MCLiteMesh] Failed to register room '%s' (contacts full)\n",
                          rs.name.c_str());
            continue;
        }
        // Cache the index that addContact just used (it's the latest slot)
        _roomContactIdx[i] = (int8_t)(getNumContacts() - 1);
        registeredRooms++;
        LOGF("[MCLiteMesh] Registered room '%s' (idx=%d, sync_since=%u)\n",
                      rs.name.c_str(), (int)_roomContactIdx[i], (unsigned)syncSince);
    }
    if (registeredRooms > 0) {
        LOGF("[MCLiteMesh] Registered %d room(s)\n", registeredRooms);
    }

    // 3. Register channels from ChannelStore
    auto& channels = ChannelStore::instance();
    for (const auto& ch : channels.all()) {
        addChannel(ch.name.c_str(),  ch.pskB64.c_str());
    }
    LOGF("[MCLiteMesh] Registered %d channels\n", (int)channels.count());

    // 4. Derive global scope transport key
    _globalScope = scopeToTransportKey(cfg.radio.scope);
    if (!_globalScope.isNull()) {
        LOGF("[MCLiteMesh] Global scope: %s\n", cfg.radio.scope.c_str());
    }
    _pathHashSize = cfg.radio.pathHashMode + 1;
    if (_pathHashSize > 1) {
        LOGF("[MCLiteMesh] Path hash size: %u bytes/hop\n", _pathHashSize);
    }

    // 5. Start the mesh
    Mesh::begin();

    _ready = true;
    LOGLN("[MCLiteMesh] Mesh started");
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

    // Check for timed-out telemetry requests (retry via flood once)
    checkTelemTimeout();

    // Free the single anon-request slot if its reply never arrived.
    if (_pendingAnonTag != 0 && (int32_t)(millis() - _pendingAnonExpiry) >= 0) {
        LOGLN("[MCLiteMesh] Anon req timed out — clearing pending slot");
        _pendingAnonTag = 0;
        memset(_pendingAnonKey, 0, PUB_KEY_SIZE);
    }

    // Free the single status-request slot if its reply never arrived.
    if (_pendingStatusTag != 0 && (int32_t)(millis() - _pendingStatusExpiry) >= 0) {
        LOGLN("[MCLiteMesh] Status req timed out — clearing pending slot");
        _pendingStatusTag = 0;
        memset(_pendingStatusKey, 0, PUB_KEY_SIZE);
    }
}

bool MCLiteMesh::advertise(const char* name, bool flood) {
    if (!_ready) return false;

    // Optionally include our own location so we appear on others' maps. Opt-in
    // (default off): adverts are broadcast unencrypted to everyone in range, so
    // this is distinct from targeted per-contact telemetry. `locationPrecision`
    // controls it: 0 = off, 32 = exact, 10-31 = coarsened to a privacy grid
    // (obfuscateCoord). We only attach a position that's still acceptable — a
    // LIVE fix, or a last-known one within gpsLastKnownMaxAge (fixStatus()
    // returns NO_FIX once it expires) — so we never broadcast a stale location.
    // NOTE: this coarsening applies ONLY to the broadcast advert; telemetry
    // responses to authorized contacts and the in-chat GPS insert stay exact.
    mesh::Packet* pkt = nullptr;
    uint8_t locPrec = ConfigManager::instance().config().locationPrecision;
    if (locPrec > 0) {
        auto& gps = GPS::instance();
        FixStatus fs = gps.fixStatus();
        if (fs == FixStatus::LIVE || fs == FixStatus::LAST_KNOWN) {
            double lat = (fs == FixStatus::LIVE) ? gps.lat() : gps.cachedLat();
            double lon = (fs == FixStatus::LIVE) ? gps.lon() : gps.cachedLon();
            if (locPrec < 32) {
                lat = obfuscateCoord(lat, locPrec);
                lon = obfuscateCoord(lon, locPrec);
            }
            pkt = createSelfAdvert(name, lat, lon);
            LOGF("[MCLiteMesh] Advertised as %s (%.5f, %.5f, prec=%u)\n", name, lat, lon, locPrec);
        }
    }
    if (!pkt) {                       // disabled or no acceptable fix → name-only
        pkt = createSelfAdvert(name);
        LOGF("[MCLiteMesh] Advertised as %s\n", name);
    }
    if (!pkt) return false;

    // Periodic adverts and the on-device button flood (mesh-wide) so repeaters
    // relay us and peers learn a return path — that's the intended default.
    // The companion app's "local advert" passes flood=false → zero-hop, which
    // reaches only direct neighbours (no relaying, no route propagation).
    if (flood) {
        sendWithScope(_globalScope, pkt, 0);
    } else {
        sendZeroHop(pkt);
    }
    return true;
}

void MCLiteMesh::sendWithScope(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis) {
    if (scope.isNull()) {
        sendFlood(pkt, delay_millis, _pathHashSize);
    } else {
        uint16_t codes[2];
        codes[0] = scope.calcTransportCode(pkt);
        codes[1] = 0;
        sendFlood(pkt, codes, delay_millis, _pathHashSize);
    }
}

void MCLiteMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
    if (_pendingSentPacketId != 0 && isMsgFloodType(pkt->getPayloadType())) {
        trackSentFp(fnv1a32(pkt->payload, pkt->payload_len), _pendingSentPacketId);
    }
    // Room contacts may have a per-room scope override (mirrors channel-scope behavior).
    // DMs always inherit the global scope today (no per-contact override exposed).
    if (recipient.type == ADV_TYPE_ROOM) {
        for (size_t i = 0; i < MAX_ROOMS_RUNTIME; i++) {
            if (_roomContactIdx[i] < 0) continue;
            ContactInfo* room_ci = getContactByIdx(_roomContactIdx[i]);
            if (room_ci && memcmp(room_ci->id.pub_key, recipient.id.pub_key, PUB_KEY_SIZE) == 0) {
                if (!_roomScope[i].isNull()) {
                    sendWithScope(_roomScope[i], pkt, delay_millis);
                    return;
                }
                break;  // matched but no override — fall through to global scope
            }
        }
    }
    sendWithScope(_globalScope, pkt, delay_millis);
}

void MCLiteMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
    if (_pendingSentPacketId != 0 && isMsgFloodType(pkt->getPayloadType())) {
        trackSentFp(fnv1a32(pkt->payload, pkt->payload_len), _pendingSentPacketId);
    }
    // Look up per-channel scope override via MeshCore channel index → ChannelStore
    int idx = findChannelIdx(channel);
    if (idx >= 0) {
        auto& store = ChannelStore::instance();
        const auto& all = store.all();
        if ((size_t)idx < all.size() && all[idx].scope.length() > 0) {
            sendWithScope(scopeToTransportKey(all[idx].scope), pkt, delay_millis);
            return;
        }
    }
    sendWithScope(_globalScope, pkt, delay_millis);
}

uint32_t MCLiteMesh::sendDM(size_t contactIdx, const char* text, uint32_t timestamp,
                             uint8_t maxRetries) {
    if (!_ready) return 0;

    ContactInfo* ci = getContactByIdx((int)contactIdx);
    if (!ci) {
        LOGLN("[MCLiteMesh] Invalid contact index for DM");
        return 0;
    }

    uint32_t packetId = allocPacketId();
    uint32_t expectedAck = 0;
    uint32_t estTimeout = 0;

    _pendingSentPacketId = packetId;
    int result = sendMessage(*ci, timestamp, 1, text, expectedAck, estTimeout);
    _pendingSentPacketId = 0;

    if (result == MSG_SEND_FAILED) {
        LOGF("[MCLiteMesh] DM send failed to %s\n", ci->name);
        return 0;
    }

    LOGF("[MCLiteMesh] DM sent to %s (packetId=%u, %s, timeout=%ums)\n",
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

int MCLiteMesh::loginRoom(size_t roomIdx, const char* password, uint32_t& estTimeout) {
    if (!_ready) return MSG_SEND_FAILED;
    if (roomIdx >= MAX_ROOMS_RUNTIME || _roomContactIdx[roomIdx] < 0) {
        LOGF("[MCLiteMesh] loginRoom: invalid room idx %u\n", (unsigned)roomIdx);
        return MSG_SEND_FAILED;
    }
    ContactInfo* ci = getContactByIdx(_roomContactIdx[roomIdx]);
    if (!ci) {
        LOGLN("[MCLiteMesh] loginRoom: contact lookup failed");
        return MSG_SEND_FAILED;
    }
    int result = sendLogin(*ci, password, estTimeout);
    LOGF("[MCLiteMesh] Login to room '%s': result=%d, est_timeout=%ums\n",
                  ci->name, result, (unsigned)estTimeout);
    return result;
}

uint32_t MCLiteMesh::sendRoomPost(size_t roomIdx, const char* text, uint32_t timestamp,
                                   uint8_t maxRetries) {
    if (!_ready) return 0;
    if (roomIdx >= MAX_ROOMS_RUNTIME || _roomContactIdx[roomIdx] < 0) {
        LOGF("[MCLiteMesh] sendRoomPost: invalid room idx %u\n", (unsigned)roomIdx);
        return 0;
    }
    int globalIdx = _roomContactIdx[roomIdx];
    ContactInfo* ci = getContactByIdx(globalIdx);
    if (!ci) {
        LOGLN("[MCLiteMesh] sendRoomPost: contact lookup failed");
        return 0;
    }

    uint32_t packetId = allocPacketId();
    uint32_t expectedAck = 0;
    uint32_t estTimeout = 0;

    _pendingSentPacketId = packetId;
    int result = sendMessage(*ci, timestamp, 1, text, expectedAck, estTimeout);
    _pendingSentPacketId = 0;

    if (result == MSG_SEND_FAILED) {
        LOGF("[MCLiteMesh] Room post failed to '%s'\n", ci->name);
        return 0;
    }

    LOGF("[MCLiteMesh] Room post sent to '%s' (packetId=%u, %s, timeout=%ums)\n",
                  ci->name, packetId,
                  result == MSG_SEND_SENT_DIRECT ? "direct" : "flood",
                  estTimeout);

    // Track ACK using same pipeline as sendDM. contactIdx stores the BaseChatMesh
    // global contact index for retry (not the per-room idx).
    AckEntry* entry = findFreeAck();
    if (entry) {
        entry->expectedAck = expectedAck;
        entry->packetId    = packetId;
        entry->timeoutMs   = millis() + estTimeout;
        entry->attempt     = 1;
        entry->maxAttempts = maxRetries;
        entry->contactIdx  = (size_t)globalIdx;
        entry->timestamp   = timestamp;
        strncpy(entry->text, text, sizeof(entry->text) - 1);
        entry->text[sizeof(entry->text) - 1] = '\0';
        entry->active      = true;
    }

    return packetId;
}

uint32_t MCLiteMesh::sendGroup(int channelIdx, const char* senderName, const char* text,
                               uint32_t timestamp) {
    if (!_ready) return 0;

    ChannelDetails cd;
    if (!getChannel(channelIdx, cd)) {
        LOGF("[MCLiteMesh] Invalid channel index %d\n", channelIdx);
        return 0;
    }

    uint32_t packetId = allocPacketId();
    _pendingSentPacketId = packetId;
    bool ok = sendGroupMessage(timestamp, cd.channel, senderName, text, strlen(text));
    _pendingSentPacketId = 0;

    if (ok) {
        LOGF("[MCLiteMesh] Group msg sent to %s (packetId=%u)\n", cd.name, packetId);
        return packetId;
    }
    return 0;
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
        LOGF("[MCLiteMesh] Retrying packet %u (attempt %d/%d)\n",
                      entry.packetId, entry.attempt, entry.maxAttempts);

        ContactInfo* ci = getContactByIdx((int)entry.contactIdx);
        if (ci) {
            uint32_t newAck = 0, newTimeout = 0;
            // Force flood routing on retries — if the direct path has degraded,
            // falling back to flood gives the mesh a better chance to deliver.
            ContactInfo floodCi = *ci;
            floodCi.out_path_len = OUT_PATH_UNKNOWN;
            int result = sendMessage(floodCi, entry.timestamp, entry.attempt,
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
    LOGF("[MCLiteMesh] Packet %u FAILED after %d attempts\n",
                  entry.packetId, entry.attempt);
    uint32_t pid = entry.packetId;
    entry.active = false;
    if (_onFail) _onFail(pid);
}

// ---- BaseChatMesh overrides ----

void MCLiteMesh::onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id,
                              uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) {
    // BaseChatMesh only stores the raw advert blob (putBlobByKey) for nodes already
    // in contacts[], so a node heard but not yet saved never gets cached — and a
    // contact saved from Heard Adverts then can't be shared until it re-adverts.
    // Capture the blob here for every *valid* advert. Mirror the base's header
    // normalization (FLOOD route, no transport codes) so the stored packet is
    // byte-identical to what shareContactZeroHop() re-broadcasts.
    AdvertDataParser parser(app_data, app_data_len);
    if (parser.isValid() && parser.hasName()) {
        uint8_t saved = packet->header;
        packet->header = (packet->header & ~PH_ROUTE_MASK) | ROUTE_TYPE_FLOOD;
        uint8_t buf[ADVERT_BLOB_MAX];
        int plen = packet->writeTo(buf);
        packet->header = saved;
        if (plen > 0 && (size_t)plen <= ADVERT_BLOB_MAX) {
            putBlobByKey(id.pub_key, PUB_KEY_SIZE, buf, plen);
        }
    }
    BaseChatMesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len);
}

void MCLiteMesh::onDiscoveredContact(ContactInfo& contact, bool is_new,
                                      uint8_t path_len, const uint8_t* path) {
    LOGF("[MCLiteMesh] Discovered contact: %s (%s, hops=%d)\n",
                  contact.name, is_new ? "new" : "update", path_len & 0x3F);

    HeardAdvertCache::instance().store(contact.id.pub_key,
                                       contact.name,
                                       contact.type,
                                       path_len,
                                       path,
                                       contact.gps_lat,
                                       contact.gps_lon);

    if (_onAdvert) _onAdvert(contact, is_new);
}

// ───────────────────────── Advert-blob store / contact sharing ─────────────────────────
//
// shareContactZeroHop() re-broadcasts a contact's *original signed advert* — the
// only way to share a contact (we can't sign an advert on someone else's behalf).
// MeshCore captures that raw packet via putBlobByKey() in onAdvertRecv; we keep a
// RAM cache of the latest advert per heard node, and back saved contacts to SD so
// sharing survives a reboot. getBlobByKey() feeds it back for the re-broadcast.

void MCLiteMesh::advertBlobPath(const uint8_t* key, char* out, size_t outLen) {
    char hex[PUB_KEY_SIZE * 2 + 1];
    for (int i = 0; i < PUB_KEY_SIZE; i++) sprintf(hex + i * 2, "%02x", key[i]);
    hex[PUB_KEY_SIZE * 2] = '\0';
    snprintf(out, outLen, "/mclite/adverts/%s.adv", hex);
}

MCLiteMesh::AdvertBlob* MCLiteMesh::findAdvertBlob(const uint8_t* key) {
    for (auto& b : _advertBlobs) {
        if (b.used && memcmp(b.key, key, PUB_KEY_SIZE) == 0) return &b;
    }
    return nullptr;
}

bool MCLiteMesh::writeAdvertBlobToSD(const uint8_t* key, const uint8_t* data, uint16_t len) {
    if (!SDCard::instance().isMounted() || len == 0) return false;
    SDCard::instance().mkdir("/mclite/adverts");
    char path[96];
    advertBlobPath(key, path, sizeof(path));
    // Hex-encode so the binary packet round-trips through the String file API.
    String hex;
    hex.reserve(len * 2);
    char b[3];
    for (uint16_t i = 0; i < len; i++) { sprintf(b, "%02x", data[i]); hex += b; }
    return SDCard::instance().writeFile(path, hex);
}

bool MCLiteMesh::loadAdvertBlobFromSD(const uint8_t* key, uint8_t* dest, uint16_t& len) {
    len = 0;
    if (!SDCard::instance().isMounted()) return false;
    char path[96];
    advertBlobPath(key, path, sizeof(path));
    if (!SDCard::instance().fileExists(path)) return false;
    String hex = SDCard::instance().readFile(path);
    // Reject a truncated/corrupt file (e.g. partial write) rather than decode it
    // to a garbage packet we'd then re-broadcast: require even length + pure hex.
    if (hex.length() == 0 || (hex.length() % 2) != 0 || !isHexString(hex)) return false;
    size_t n = hex.length() / 2;
    if (n > ADVERT_BLOB_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char pair[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        dest[i] = (uint8_t)strtoul(pair, nullptr, 16);
    }
    len = (uint16_t)n;
    return true;
}

bool MCLiteMesh::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) {
    if (key_len != PUB_KEY_SIZE || len <= 0 || (size_t)len > ADVERT_BLOB_MAX) return false;

    // Reuse this key's slot, else a free slot, else evict the least-recently-used.
    AdvertBlob* slot = findAdvertBlob(key);
    if (!slot) for (auto& b : _advertBlobs) { if (!b.used) { slot = &b; break; } }
    if (!slot) { slot = &_advertBlobs[0]; for (auto& b : _advertBlobs) if (b.seq < slot->seq) slot = &b; }

    memcpy(slot->key, key, PUB_KEY_SIZE);
    memcpy(slot->data, src_buf, len);
    slot->len  = (uint16_t)len;
    slot->seq  = ++_advertBlobSeq;
    slot->used = true;

    // Backfill SD once for known (config) contacts so Share works after a reboot,
    // without writing on every advert (skip when a file already exists / not a contact).
    if (lookupContactByPubKey(key, PUB_KEY_SIZE)) {
        char path[96];
        advertBlobPath(key, path, sizeof(path));
        if (!SDCard::instance().fileExists(path)) writeAdvertBlobToSD(key, slot->data, slot->len);
    }
    return true;
}

int MCLiteMesh::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
    if (key_len != PUB_KEY_SIZE) return 0;
    if (AdvertBlob* slot = findAdvertBlob(key)) {
        memcpy(dest_buf, slot->data, slot->len);
        return slot->len;
    }
    // Fall back to a persisted blob (saved contact heard in a previous session).
    uint16_t len = 0;
    if (loadAdvertBlobFromSD(key, dest_buf, len)) return len;
    return 0;
}

bool MCLiteMesh::hasAdvertBlob(const uint8_t* pubKey) {
    if (findAdvertBlob(pubKey)) return true;
    char path[96];
    advertBlobPath(pubKey, path, sizeof(path));
    return SDCard::instance().isMounted() && SDCard::instance().fileExists(path);
}

bool MCLiteMesh::persistAdvertBlobForKey(const uint8_t* pubKey) {
    AdvertBlob* slot = findAdvertBlob(pubKey);
    if (!slot) return false;
    return writeAdvertBlobToSD(pubKey, slot->data, slot->len);
}

void MCLiteMesh::deleteAdvertBlob(const uint8_t* pubKey) {
    if (AdvertBlob* slot = findAdvertBlob(pubKey)) { slot->used = false; slot->len = 0; }
    if (SDCard::instance().isMounted()) {
        char path[96];
        advertBlobPath(pubKey, path, sizeof(path));
        if (SDCard::instance().fileExists(path)) SDCard::instance().remove(path);
    }
}

bool MCLiteMesh::shareContact(const uint8_t* pubKey) {
    ContactInfo* ci = lookupContactByPubKey(pubKey, PUB_KEY_SIZE);
    if (!ci) return false;
    return shareContactZeroHop(*ci);
}

void MCLiteMesh::trackSentFp(uint32_t fp, uint32_t packetId) {
    if (fp == 0 || packetId == 0) return;
    for (int i = 0; i < ECHO_SLOTS; i++) {
        if (_echoSlots[i].active && _echoSlots[i].fp == fp) return;  // already tracked
    }
    auto& s = _echoSlots[_echoIdx];
    s.fp = fp; s.packetId = packetId; s.active = true; s.fired = false;
    _echoIdx = (_echoIdx + 1) % ECHO_SLOTS;
}

void MCLiteMesh::logRxRaw(float /*snr*/, float /*rssi*/, const uint8_t raw[], int len) {
    if (len < 2) return;
    const uint8_t ptype = (raw[0] >> 2) & 0x0F;
    if (!isMsgFloodType(ptype)) return;

    const uint8_t rt    = raw[0] & 0x03;
    const bool    has_xp = (rt == ROUTE_TYPE_TRANSPORT_FLOOD || rt == ROUTE_TYPE_TRANSPORT_DIRECT);
    const int     ps    = 1 + (has_xp ? 4 : 0);
    if (ps >= len) return;

    const uint8_t pb    = raw[ps];
    const uint8_t cnt_p = pb & 0x3F;
    const uint8_t hsz_p = (uint8_t)(((pb >> 6) & 0x03) + 1);
    const int     poff  = ps + 1 + cnt_p * hsz_p;
    if (poff >= len) return;

    // Only match packets that have passed through at least one repeater.
    if (cnt_p < 1) return;

    uint32_t fp = fnv1a32(raw + poff, len - poff);
    for (int i = 0; i < ECHO_SLOTS; i++) {
        if (_echoSlots[i].active && !_echoSlots[i].fired && _echoSlots[i].fp == fp) {
            _echoSlots[i].fired = true;
            LOGF("[MCLiteMesh] Echo detected for packet %u\n", _echoSlots[i].packetId);
            if (_onEchoDetected) _onEchoDetected(_echoSlots[i].packetId);
            return;
        }
    }
}

ContactInfo* MCLiteMesh::processAck(const uint8_t* data) {
    uint32_t ackHash;
    memcpy(&ackHash, data, 4);

    AckEntry* entry = findAckByHash(ackHash);
    if (entry) {
        uint32_t pid = entry->packetId;
        size_t cidx = entry->contactIdx;
        entry->active = false;

        LOGF("[MCLiteMesh] ACK received for packet %u\n", pid);

        if (_onAck) _onAck(pid);

        // Return the contact for MeshCore's internal bookkeeping
        return getContactByIdx((int)cidx);
    }

    return nullptr;
}

void MCLiteMesh::onContactPathUpdated(const ContactInfo& contact) {
    LOGF("[MCLiteMesh] Path updated for %s (hops=%d)\n",
                  contact.name, contact.out_path_len);
}

void MCLiteMesh::onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt,
                                uint32_t sender_timestamp, const char* text) {
    LOGF("[MCLiteMesh] DM from %s: %s\n", contact.name, text);
    if (_onMessage) _onMessage(contact, sender_timestamp, text);
}

void MCLiteMesh::onCommandDataRecv(const ContactInfo& contact, mesh::Packet* pkt,
                                    uint32_t sender_timestamp, const char* text) {
    // CLI commands not used in MCLite — ignore
    LOGF("[MCLiteMesh] CLI data from %s (ignored)\n", contact.name);
}

void MCLiteMesh::onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt,
                                      uint32_t sender_timestamp,
                                      const uint8_t* sender_prefix, const char* text) {
    // Room post: dispatch with sender_prefix (4 bytes) so UIManager can resolve
    // the sender's alias against ContactStore. BaseChatMesh has already advanced
    // contact.sync_since by the time we get here (BaseChatMesh.cpp:242-243).
    if (contact.type == ADV_TYPE_ROOM) {
        LOGF("[MCLiteMesh] Room msg from '%s': %s\n", contact.name, text);
        if (_onRoomMsg) _onRoomMsg(contact, sender_prefix, sender_timestamp, text);
        return;
    }
    // Signed DM fallback (no current MCLite path produces these, but BaseChatMesh
    // requires the override)
    LOGF("[MCLiteMesh] Signed DM from %s: %s\n", contact.name, text);
    if (_onMessage) _onMessage(contact, sender_timestamp, text);
}

void MCLiteMesh::onChannelMessageRecv(const mesh::GroupChannel& channel,
                                       mesh::Packet* pkt,
                                       uint32_t timestamp, const char* text) {
    LOGF("[MCLiteMesh] Group msg on channel: %s\n", text);
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
    LOGLN("[MCLiteMesh] Internal send timeout");
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
        LOGF("[MCLiteMesh] Telemetry request from unknown contact %s — ignored\n", contact.name);
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
        LOGF("[MCLiteMesh] Telemetry request from %s denied (no base permission)\n", contact.name);
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

    LOGF("[MCLiteMesh] Telemetry response to %s: %d bytes LPP\n", contact.name, tlen);
    return 4 + tlen;
}

bool MCLiteMesh::outboundBusy() const {
    return _mgr && _mgr->getOutboundCount(_ms->getMillis()) > 0;
}

bool MCLiteMesh::sendAnonReqByKey(const uint8_t* pubKey, const uint8_t* data, uint8_t len,
                                  uint32_t& tag, uint32_t& estTimeout) {
    if (!_ready || !pubKey || !data || len == 0) return false;

    // Resolve to a known contact, or register a transient anon contact for the send.
    // ADV_TYPE_NONE is never persisted (MCLite only writes config-defined contacts,
    // shouldAutoAddContactType() stays false) and is filtered out of GET_CONTACTS.
    ContactInfo* ci = lookupContactByPubKey(pubKey, PUB_KEY_SIZE);
    ContactInfo anon;
    if (!ci) {
        memset(&anon, 0, sizeof(anon));
        memcpy(anon.id.pub_key, pubKey, PUB_KEY_SIZE);
        anon.out_path_len = 0;          // zero-hop direct by default
        anon.type = ADV_TYPE_NONE;      // unknown / transient (mirrors companion_radio)
        if (!addContact(anon)) {
            LOGLN("[MCLiteMesh] Anon req: no contact slot");
            return false;
        }
        ci = &anon;                     // local struct as recipient, like the reference
    }

    tag = 0;
    int result = sendAnonReq(*ci, data, len, tag, estTimeout);
    if (result == MSG_SEND_FAILED) {
        LOGF("[MCLiteMesh] Anon req send failed to %s\n", ci->name);
        return false;
    }
    _pendingAnonTag = tag;
    memcpy(_pendingAnonKey, ci->id.pub_key, PUB_KEY_SIZE);
    // Free the single slot if no reply arrives (estimate + slack), so a dropped
    // reply doesn't wedge the feature until reboot. A late reply past this just
    // finds no pending request and is ignored (the app has its own timeout).
    _pendingAnonExpiry = millis() + estTimeout + 5000;
    LOGF("[MCLiteMesh] Anon req sent (tag=%u, timeout=%ums)\n", tag, estTimeout);
    return true;
}

bool MCLiteMesh::sendStatusReqByKey(const uint8_t* pubKey, uint32_t& tag, uint32_t& estTimeout) {
    if (!_ready || !pubKey) return false;
    ContactInfo* ci = lookupContactByPubKey(pubKey, PUB_KEY_SIZE);
    if (!ci) {
        LOGLN("[MCLiteMesh] Status req: unknown contact key");
        return false;
    }
    tag = 0;
    int result = sendRequest(*ci, REQ_TYPE_GET_STATUS, tag, estTimeout);
    if (result == MSG_SEND_FAILED) {
        LOGF("[MCLiteMesh] Status req send failed to %s\n", ci->name);
        return false;
    }
    _pendingStatusTag = tag;
    memcpy(_pendingStatusKey, ci->id.pub_key, PUB_KEY_SIZE);
    _pendingStatusExpiry = millis() + estTimeout + 5000;   // free the slot if no reply (see anon-req)
    LOGF("[MCLiteMesh] Status req sent (tag=%u, timeout=%ums)\n", tag, estTimeout);
    return true;
}

bool MCLiteMesh::sendTracePath(uint32_t tag, uint32_t auth, uint8_t flags,
                               const uint8_t* path, uint8_t path_len, uint32_t& estTimeout) {
    if (!_ready) return false;
    // createTrace seeds payload_len = 9 (tag+auth+flags); sendDirect() then appends the
    // whole path to the payload (trace path is NOT bounded by MAX_PATH_SIZE). Reject any
    // path that wouldn't fit so the append can't overflow Packet::payload[MAX_PACKET_PAYLOAD].
    if ((uint32_t)path_len + 9 > MAX_PACKET_PAYLOAD) {
        LOGF("[MCLiteMesh] Trace: path too long (%u)\n", path_len);
        return false;
    }
    mesh::Packet* pkt = createTrace(tag, auth, flags);
    if (!pkt) {
        LOGLN("[MCLiteMesh] Trace: packet pool empty");
        return false;
    }
    uint8_t path_sz = flags & 0x03;
    sendDirect(pkt, path, path_len);
    uint32_t airtime = _radio ? _radio->getEstAirtimeFor(pkt->payload_len + pkt->path_len + 2) : 0;
    estTimeout = calcDirectTimeoutMillisFor(airtime, path_len >> path_sz);
    LOGF("[MCLiteMesh] Trace sent (tag=%u, hops=%u)\n", tag, (unsigned)(path_len >> path_sz));
    return true;
}

void MCLiteMesh::onTraceRecv(mesh::Packet* packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                             const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) {
    LOGF("[MCLiteMesh] Trace reply (tag=%u, path_len=%u)\n", tag, path_len);
    if (_onTrace)
        _onTrace(tag, auth_code, flags, path_snrs, path_hashes, path_len,
                 (int8_t)(packet->getSNR() * 4));
}

bool MCLiteMesh::requestTelemetry(size_t contactIdx, uint32_t& estTimeout) {
    if (!_ready) return false;

    ContactInfo* ci = getContactByIdx((int)contactIdx);
    if (!ci) {
        LOGLN("[MCLiteMesh] Invalid contact index for telemetry request");
        return false;
    }

    uint32_t tag = 0;
    int result = sendRequest(*ci, REQ_TYPE_GET_TELEMETRY_DATA, tag, estTimeout);
    if (result == MSG_SEND_FAILED) {
        LOGF("[MCLiteMesh] Telemetry request failed to %s\n", ci->name);
        return false;
    }

    _pendingTelemTag = tag;
    memcpy(_pendingTelemKey, ci->id.pub_key, PUB_KEY_SIZE);
    
    // Track for potential flood retry on timeout
    _telemRetry.active = true;
    _telemRetry.timeoutMs = millis() + estTimeout;
    _telemRetry.contactIdx = contactIdx;
    _telemRetry.tag = tag;
    _telemRetry.retried = false;
    
    LOGF("[MCLiteMesh] Telemetry requested from %s (timeout=%ums)\n",
                  ci->name, estTimeout);
    return true;
}

bool MCLiteMesh::requestTelemetryByKey(const uint8_t* pubKey, uint32_t& estTimeout) {
    if (!_ready || !pubKey) return false;
    // contacts[] is private in BaseChatMesh, so resolve the index via the public
    // iterator (same index space getContactByIdx / requestTelemetry use).
    int n = getNumContacts();
    for (int idx = 0; idx < n; idx++) {
        ContactInfo* ci = getContactByIdx(idx);
        if (ci && memcmp(ci->id.pub_key, pubKey, PUB_KEY_SIZE) == 0) {
            return requestTelemetry((size_t)idx, estTimeout);
        }
    }
    LOGLN("[MCLiteMesh] Telemetry request: unknown contact key");
    return false;
}

void MCLiteMesh::checkTelemTimeout() {
    if (!_telemRetry.active) return;

    uint32_t now = millis();
    if ((int32_t)(now - _telemRetry.timeoutMs) < 0) return;

    // The flood retry already went out and also timed out with no reply:
    // finalize the exchange and release the single-slot pending state. Without
    // this the UI masks it via its own timeout (clearPendingTelemetry), but the
    // background auto-refresh has no such timeout — a stuck _pendingTelemTag
    // would make telemetryPending() true forever and block every future scan.
    if (_telemRetry.retried) {
        clearPendingTelemetry();
        return;
    }

    // If outbound queue still has packets, extend timeout instead of retrying.
    // Push the UI's parallel timeout out in lockstep — otherwise it fires this
    // same tick (UIManager::update runs right after MeshManager::update), shows
    // "no response", and clears _telemRetry, cancelling this deferred retry
    // exactly when the mesh is congested and a retry matters most.
    if (_mgr->getOutboundCount(_ms->getMillis()) > 0) {
        _telemRetry.timeoutMs = now + 2000;
        if (_onTelemetryRetry) _onTelemetryRetry(2000);
        return;
    }

    // Timeout — retry once via flood
    ContactInfo* ci = getContactByIdx((int)_telemRetry.contactIdx);
    if (ci) {
        LOGF("[MCLiteMesh] Telemetry timeout for %s -- retrying via flood\n", ci->name);
        ContactInfo flood = *ci;
        flood.out_path_len = OUT_PATH_UNKNOWN;
        uint32_t newTag = 0, newTimeout = 0;
        int result = sendRequest(flood, REQ_TYPE_GET_TELEMETRY_DATA, newTag, newTimeout);
        if (result != MSG_SEND_FAILED) {
            _pendingTelemTag = newTag;
            _telemRetry.tag = newTag;
            _telemRetry.retried = true;
            _telemRetry.timeoutMs = now + newTimeout;
            LOGF("[MCLiteMesh] Telemetry flood retry sent to %s (timeout=%ums)\n",
                 ci->name, newTimeout);
            if (_onTelemetryRetry) _onTelemetryRetry(newTimeout);
            return;
        }
        LOGF("[MCLiteMesh] Telemetry flood retry failed to %s\n", ci->name);
    }

    // Give up — release pending so the slot isn't held by a dead request.
    clearPendingTelemetry();
}
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
            LOGF("[MCLiteMesh] Unknown LPP type %d — skipping rest of payload\n", type);
            return -1;
    }
}

void MCLiteMesh::onContactResponse(const ContactInfo& contact,
                                    const uint8_t* data, uint8_t len) {
    LOGF("[MCLiteMesh] Response from %s (%d bytes)\n", contact.name, len);

    // NOTE: room-server login response includes data[5] = legacy keep_alive_secs/16,
    // currently always 0 in simple_room_server. The canonical companion_radio client
    // gates BaseChatMesh::startConnection() on data[5] > 0; we follow suit and never
    // call it. If a server ever advertises data[5] != 0, startConnection(contact,
    // data[5] * 16) must be called on login OK and checkConnections() ticked from
    // MCLiteMesh::loop(). References:
    //   simple_room_server/MyMesh.cpp:368   (server writes 0)
    //   companion_radio/MyMesh.cpp:674-676  (gates startConnection on >0)
    if (contact.type == ADV_TYPE_ROOM && len > 5 && data[5] != 0) {
        LOGF("[MCLiteMesh] WARN: room '%s' advertised keep_alive_secs=%u — "
                      "server has re-enabled keep-alive; consider startConnection().\n",
                      contact.name, (unsigned)data[5] * 16);
    }

    // Room login response (13 bytes per simple_room_server/MyMesh.cpp:364-373):
    //   [0..3] reflected timestamp/tag    [4]   status (RESP_SERVER_LOGIN_OK=0)
    //   [5]    legacy keep_alive (see above)    [6]   legacy permissions
    //   [7]    v7 ACL permissions               [8..11] random uniqueness
    //   [12]   firmware version level
    // Dispatch BEFORE the telemetry-tag checks: rooms never send telemetry, and
    // a login response has no _pendingTelemTag, which would silently drop it.
    if (contact.type == ADV_TYPE_ROOM) {
        if (len < 7) return;  // need at least status + legacy perms
        uint8_t status      = data[4];
        uint8_t permissions = data[6];
        uint8_t aclPerms    = (len > 7)  ? data[7]  : permissions;  // v7 ACL perms (fallback: legacy)
        uint8_t fwLevel     = (len > 12) ? data[12] : 0;            // server firmware level
        if (_onRoomLogin) _onRoomLogin(contact, status, permissions, aclPerms, fwLevel);
        return;
    }

    // Need at least 4 bytes (reflected timestamp) + some payload
    if (len <= 4) return;

    // Anonymous-request reply (CMD_SEND_ANON_REQ): match the reflected tag + sender
    // key and forward the verbatim payload to the companion. Checked BEFORE the
    // telemetry path so it isn't swallowed by the "no pending telemetry" early return.
    {
        uint32_t anonTag;
        memcpy(&anonTag, data, 4);
        if (_pendingAnonTag != 0 && anonTag == _pendingAnonTag &&
            memcmp(contact.id.pub_key, _pendingAnonKey, PUB_KEY_SIZE) == 0) {
            _pendingAnonTag = 0;
            memset(_pendingAnonKey, 0, PUB_KEY_SIZE);
            if (_onAnonResponse) _onAnonResponse(anonTag, data + 4, len - 4);
            return;
        }
    }

    // Status-request reply (CMD_SEND_STATUS_REQ): match the reflected tag + sender key,
    // forward the verbatim status blob (data+4) to the companion. Same placement reason
    // as the anon branch above.
    {
        uint32_t statusTag;
        memcpy(&statusTag, data, 4);
        if (_pendingStatusTag != 0 && statusTag == _pendingStatusTag &&
            memcmp(contact.id.pub_key, _pendingStatusKey, PUB_KEY_SIZE) == 0) {
            _pendingStatusTag = 0;
            memset(_pendingStatusKey, 0, PUB_KEY_SIZE);
            if (_onStatusResponse) _onStatusResponse(contact.id.pub_key, data + 4, len - 4);
            return;
        }
    }

    // Validate: must have a pending request, from the expected contact
    if (_pendingTelemTag == 0) {
        LOGLN("[MCLiteMesh] Unexpected response (no pending request)");
        return;
    }
    if (memcmp(contact.id.pub_key, _pendingTelemKey, PUB_KEY_SIZE) != 0) {
        LOGF("[MCLiteMesh] Response from wrong contact %s — ignored\n", contact.name);
        return;
    }

    // Validate reflected tag matches
    uint32_t reflectedTag;
    memcpy(&reflectedTag, data, 4);
    if (reflectedTag != _pendingTelemTag) {
        LOGLN("[MCLiteMesh] Response tag mismatch — ignored");
        return;
    }

    // Parse CayenneLPP from data+4
    const uint8_t* lpp = data + 4;
    uint8_t lppLen = len - 4;

    TelemetryData telem;
    telem.receivedAt = millis();

    uint8_t pos = 0;
    while (pos + 2 <= lppLen) {
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
    _telemRetry.active = false;

    if (_onTelemetry) _onTelemetry(contact, telem);
    // Also hand the raw CayenneLPP up verbatim — the companion app forwards it to
    // the phone, which parses it itself (standard PUSH_CODE_TELEMETRY_RESPONSE).
    if (_onTelemetryRaw) _onTelemetryRaw(contact.id.pub_key, lpp, lppLen);
}

float MCLiteMesh::getAirtimeBudgetFactor() const {
    // EU 868-870 MHz (ETSI EN 300 220, G3 sub-band): 10% duty cycle → factor 9.0
    // All other frequencies: MeshCore default 33% → factor 2.0
    if (_frequency >= 868.0f && _frequency <= 870.0f) return 9.0f;
    return 2.0f;
}

}  // namespace mclite
