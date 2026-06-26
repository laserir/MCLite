#pragma once

// MAX_CONTACTS and MAX_GROUP_CHANNELS are defined as build flags in platformio.ini
// (they must be set before BaseChatMesh.h is compiled)

#include <CayenneLPP.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/TransportKeyStore.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/radiolib/CustomSX1262.h>
#include <RadioLib.h>
#include <functional>
#include "../storage/TelemetryCache.h"

namespace mclite {

// MeshCore telemetry protocol constants
static constexpr uint8_t REQ_TYPE_GET_TELEMETRY_DATA = 0x03;
static constexpr uint8_t TELEM_PERM_BASE        = 0x01;
static constexpr uint8_t TELEM_PERM_LOCATION     = 0x02;
static constexpr uint8_t TELEM_PERM_ENVIRONMENT  = 0x04;
static constexpr uint8_t TELEM_CHANNEL_SELF      = 1;

// Callback types for incoming events
using MeshMessageCb  = std::function<void(const ContactInfo& contact,
                                           uint32_t timestamp, const char* text)>;
using MeshGroupMsgCb = std::function<void(const mesh::GroupChannel& channel,
                                           uint32_t timestamp, const char* text)>;
using MeshAckCb      = std::function<void(uint32_t packetId)>;
using MeshFailCb     = std::function<void(uint32_t packetId)>;
using MeshAdvertCb   = std::function<void(const ContactInfo& contact, bool isNew)>;
using MeshTelemetryCb = std::function<void(const ContactInfo& contact, const TelemetryData& data)>;
// Raw CayenneLPP payload of a telemetry reply (pubKey is 32 B). Used by the
// companion app, which forwards the verbatim LPP to the phone for it to parse.
using MeshTelemetryRawCb = std::function<void(const uint8_t* pubKey, const uint8_t* lpp, uint8_t lppLen)>;
using MeshTelemetryRetryCb = std::function<void(uint32_t newTimeoutMs)>;
// Raw reply to an anonymous request (CMD_SEND_ANON_REQ). Carries the request tag
// (so the app matches it to RESP_CODE_SENT) and the verbatim response payload.
using MeshAnonRespCb = std::function<void(uint32_t tag, const uint8_t* data, uint8_t len)>;
// Reply to a status request (CMD_SEND_STATUS_REQ): sender pubkey + verbatim status blob.
using MeshStatusRespCb = std::function<void(const uint8_t* pubKey, const uint8_t* data, uint8_t len)>;
// Trace reply (CMD_SEND_TRACE_PATH → onTraceRecv): per-hop path hashes + SNRs and a final SNR.
using MeshTraceCb = std::function<void(uint32_t tag, uint32_t auth, uint8_t flags,
                                       const uint8_t* path_snrs, const uint8_t* path_hashes,
                                       uint8_t path_len, int8_t final_snr)>;
using MeshRoomMsgCb   = std::function<void(const ContactInfo& contact,
                                            const uint8_t* sender_prefix /* 4 B */,
                                            uint32_t sender_timestamp,
                                            const char* text)>;
using MeshRoomLoginCb = std::function<void(const ContactInfo& contact,
                                            uint8_t status, uint8_t permissions,
                                            uint8_t aclPerms, uint8_t fwLevel)>;
// Fired when a repeater is heard re-broadcasting one of our sent messages.
using MeshEchoDetectedCb = std::function<void(uint32_t packetId)>;

// Pending ACK entry
struct AckEntry {
    uint32_t expectedAck = 0;   // MeshCore ACK hash to match
    uint32_t packetId    = 0;   // Our internal packet ID
    uint32_t timeoutMs   = 0;   // When ACK expires (millis target)
    uint8_t  attempt     = 0;
    uint8_t  maxAttempts = 3;
    size_t   contactIdx  = 0;   // Contact index for retry
    char     text[161];         // Message text for retry (MAX_TEXT_LEN + 1)
    uint32_t timestamp   = 0;
    bool     active      = false;
};

// Pending telemetry retry entry
struct TelemRetry {
    bool     active = false;
    uint32_t timeoutMs = 0;
    size_t   contactIdx = 0;
    uint32_t tag = 0;
    bool     retried = false;
};

static constexpr int PACKET_POOL_SIZE = 12;
static constexpr int MAX_PENDING_ACKS = 16;

class MCLiteMesh : public BaseChatMesh {
public:
    MCLiteMesh(CustomSX1262& radio, SimpleMeshTables& tables);
    ~MCLiteMesh();

    // Initialize: load identity, register contacts/channels, begin mesh
    bool begin(const char* deviceName);

    // Main loop — call from MeshManager::update()
    void loop();

    // Send advertisement (name + optional opt-in location).
    // flood = true (default) re-broadcasts mesh-wide via the global scope so
    // repeaters relay it and the mesh learns a return path to us — this is what
    // the periodic timer and the on-device advert button use, intentionally.
    // flood = false sends zero-hop (neighbours only), used for the companion
    // app's "local advert" option.
    bool advertise(const char* name, bool flood = true);

    // Region/flood-scope helpers for the companion. deriveScopeKey computes the same
    // transport key MCLite uses for a scope string (SHA256("#name")[:16]; "*"/"" => null),
    // so the app's name+key can be verified/answered. setGlobalScope overrides the live
    // session scope (companion CMD_SET_FLOOD_SCOPE_KEY) — not persisted, reverts on reboot.
    static void deriveScopeKey(const String& scope, uint8_t out[16]);
    void setGlobalScope(const uint8_t key[16]);   // all-zero key => null (no scope)

    // Serialize our own advert into out[] for companion EXPORT_CONTACT (self). Returns the blob
    // length (0 on failure). Name-only — matches MCLite's 0,0 SELF_INFO location reporting.
    uint8_t exportSelf(const char* name, uint8_t out[]);

    // Held advert blob for a contact (companion EXPORT_CONTACT). Public wrapper over the
    // protected getBlobByKey. Returns the blob length, 0 if none held.
    uint8_t exportContactBlob(const uint8_t* pubKey, uint8_t out[]);

    // Send DM — returns internal packetId, 0 on failure
    uint32_t sendDM(size_t contactIdx, const char* text, uint32_t timestamp,
                    uint8_t maxRetries);

    // Send group message — returns internal packetId (0 on failure)
    uint32_t sendGroup(int channelIdx, const char* senderName, const char* text,
                       uint32_t timestamp);

    // Send room post — returns internal packetId, 0 on failure (mirrors sendDM)
    uint32_t sendRoomPost(size_t roomIdx, const char* text, uint32_t timestamp,
                          uint8_t maxRetries);

    // Send room login. Locates the registered ROOM contact for roomIdx and calls
    // BaseChatMesh::sendLogin. Returns the int result of sendLogin
    // (MSG_SEND_FAILED / SENT_FLOOD / SENT_DIRECT). estTimeout filled on success.
    int loginRoom(size_t roomIdx, const char* password, uint32_t& estTimeout);

    // Register callbacks
    void onMessage(MeshMessageCb cb)   { _onMessage = cb; }
    void onGroupMsg(MeshGroupMsgCb cb) { _onGroupMsg = cb; }
    void onAck(MeshAckCb cb)           { _onAck = cb; }
    void onFail(MeshFailCb cb)         { _onFail = cb; }
    void onAdvert(MeshAdvertCb cb)     { _onAdvert = cb; }
    void onTelemetry(MeshTelemetryCb cb) { _onTelemetry = cb; }
    void onTelemetryRaw(MeshTelemetryRawCb cb) { _onTelemetryRaw = cb; }
    void onTelemetryRetry(MeshTelemetryRetryCb cb) { _onTelemetryRetry = cb; }
    void onAnonResponse(MeshAnonRespCb cb) { _onAnonResponse = cb; }
    void onStatusResponse(MeshStatusRespCb cb) { _onStatusResponse = cb; }
    void onTrace(MeshTraceCb cb) { _onTrace = cb; }
    void onRoomMsg(MeshRoomMsgCb cb)           { _onRoomMsg = cb; }
    void onRoomLogin(MeshRoomLoginCb cb)       { _onRoomLogin = cb; }
    void onEchoDetected(MeshEchoDetectedCb cb) { _onEchoDetected = cb; }

    // Request telemetry from a contact — returns true on success
    bool requestTelemetry(size_t contactIdx, uint32_t& estTimeout);

    // Same, but addressed by 32-byte pubkey (used by the companion app, which has
    // the key, not an index). Resolves to the contact's index and delegates to
    // requestTelemetry. Returns false if the key isn't a known contact or send fails.
    bool requestTelemetryByKey(const uint8_t* pubKey, uint32_t& estTimeout);

    // Clear pending telemetry state (call on timeout)
    void clearPendingTelemetry() { _pendingTelemTag = 0; memset(_pendingTelemKey, 0, PUB_KEY_SIZE); _telemRetry.active = false; }

    // True while a telemetry request (UI or auto) is awaiting a reply. Telemetry
    // is single-slot, so the auto-refresh scheduler uses this to avoid clobbering
    // a manual request and to serialize its own.
    bool telemetryPending() const { return _pendingTelemTag != 0; }

    // Send an anonymous request (CMD_SEND_ANON_REQ) to a node by pubkey. If the key
    // isn't a known contact, a transient ADV_TYPE_NONE contact is registered for the
    // send (not persisted). Fills tag + estTimeout; reply arrives via onAnonResponse.
    // Single-slot like telemetry. Returns false on send failure.
    bool sendAnonReqByKey(const uint8_t* pubKey, const uint8_t* data, uint8_t len,
                          uint32_t& tag, uint32_t& estTimeout);
    bool anonReqPending() const { return _pendingAnonTag != 0; }

    // Send a status request (CMD_SEND_STATUS_REQ) to a known contact by pubkey. Fills
    // tag + estTimeout; reply arrives via onStatusResponse. Single-slot. Returns false
    // if the key isn't a known contact or the send fails.
    bool sendStatusReqByKey(const uint8_t* pubKey, uint32_t& tag, uint32_t& estTimeout);
    bool statusReqPending() const { return _pendingStatusTag != 0; }

    // Send a path trace (CMD_SEND_TRACE_PATH) along an explicit path. Fills estTimeout;
    // the reply arrives asynchronously via onTrace. Returns false on packet-pool failure.
    bool sendTracePath(uint32_t tag, uint32_t auth, uint8_t flags,
                       const uint8_t* path, uint8_t path_len, uint32_t& estTimeout);

    // True if the radio's outbound packet queue still has packets pending.
    bool outboundBusy() const;

    // Access contacts managed by BaseChatMesh
    ContactInfo* getContactByIdx(int idx);

    // This node's 32-byte public key (mesh::Mesh::self_id). Stable after begin().
    const uint8_t* selfPubKey() const { return self_id.pub_key; }

    bool isReady() const { return _ready; }

    // Set configured frequency (must be called before begin())
    void setFrequency(float freq) { _frequency = freq; }

    // Map a normal-mesh frequency to its closest offgrid whitelist band (433/869/918).
    // Deterministic so the offgrid network is always interoperable.
    static float offgridFreqFor(float normalFreq);

    // ─── Contact sharing (zero-hop advert re-broadcast) ───
    // Re-broadcast a saved contact's original signed advert so nearby nodes hear
    // it and can add the contact (it lands in their Heard Adverts). Uses the raw
    // advert blob captured by put/getBlobByKey. Returns false if no blob is held
    // for this pubkey (never heard / not persisted) or the send fails.
    bool shareContact(const uint8_t* pubKey);
    // True if a re-broadcastable advert blob exists for this pubkey (RAM or SD).
    bool hasAdvertBlob(const uint8_t* pubKey);
    // Persist this pubkey's in-RAM advert blob to SD (call right after a contact
    // is saved, so Share survives a reboot). No-op if no RAM blob is held.
    bool persistAdvertBlobForKey(const uint8_t* pubKey);
    // Remove a contact's persisted advert blob (call on contact removal).
    void deleteAdvertBlob(const uint8_t* pubKey);

protected:
    // ---- Required BaseChatMesh overrides ----

    // Capture the raw advert blob for every heard node (not just existing
    // contacts, which is all the base class stores) so a contact saved from
    // Heard Adverts can be shared immediately and after a reboot. Delegates to
    // BaseChatMesh for the actual contact/discovery handling.
    void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp,
                      const uint8_t* app_data, size_t app_data_len) override;

    void onDiscoveredContact(ContactInfo& contact, bool is_new,
                             uint8_t path_len, const uint8_t* path) override;

    ContactInfo* processAck(const uint8_t* data) override;

    void onContactPathUpdated(const ContactInfo& contact) override;

    void onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt,
                       uint32_t sender_timestamp, const char* text) override;

    void onCommandDataRecv(const ContactInfo& contact, mesh::Packet* pkt,
                           uint32_t sender_timestamp, const char* text) override;

    void onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt,
                             uint32_t sender_timestamp,
                             const uint8_t* sender_prefix, const char* text) override;

    void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt,
                              uint32_t timestamp, const char* text) override;

    uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
    uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis,
                                         uint8_t path_len) const override;

    void onSendTimeout() override;

    uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp,
                             const uint8_t* data, uint8_t len,
                             uint8_t* reply) override;

    void onContactResponse(const ContactInfo& contact,
                           const uint8_t* data, uint8_t len) override;

    // Trace reply (CMD_SEND_TRACE_PATH). Forwards per-hop path data to the companion.
    void onTraceRecv(mesh::Packet* packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                     const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) override;

    void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0) override;
    void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0) override;
    void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;

    // ---- Optional overrides ----
    bool shouldAutoAddContactType(uint8_t type) const override { return false; }  // MCLite uses config-defined contacts only

    // Advert-blob persistence — store/retrieve the raw signed advert packet keyed
    // by contact pubkey, which is what shareContactZeroHop() re-broadcasts. RAM
    // cache holds the latest advert for every heard node; saved contacts are also
    // backed to SD so sharing survives reboots (see advert-blob members below).
    bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) override;
    int  getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) override;

    // Offgrid / client-repeat: forward packets for other nodes when enabled.
    // Base-class dedup via _tables->hasSeen() prevents loops at every forward site.
    bool allowPacketForward(const mesh::Packet* packet) override { return _offgridEnabled; }

    // Airtime budget: EU 868-870 MHz → 9.0 (10% duty cycle, ETSI G3),
    //                 otherwise       → 2.0 (33% duty cycle, MeshCore default)
    float getAirtimeBudgetFactor() const override;

private:
    bool _ready = false;
    float _frequency = 0.0f;  // Configured radio frequency (MHz)
    bool _offgridEnabled = false;  // Mirror of config.offgrid.enabled, read at begin()
    TransportKey _globalScope;  // Derived from RadioConfig::scope at begin()
    uint8_t _pathHashSize = 1;  // 1/2/3 bytes per hop — wire value passed to sendFlood()
    void sendWithScope(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis);

    // ─── Advert-blob cache (backs contact sharing) ───
    // Sized to a full transport unit so a multi-hop advert (header + path + payload)
    // is never rejected; shareContactZeroHop re-sends the raw packet verbatim.
    static constexpr size_t ADVERT_BLOB_MAX   = MAX_TRANS_UNIT;
    // Match the Heard-Adverts list capacity (HeardAdvertCache HEARD_ADVERT_CAP=64),
    // not MAX_CONTACTS: we cache blobs for every heard node so that anything still
    // visible in the heard list can be saved AND shared. Saved/config contacts are
    // additionally backed to SD, so RAM eviction never disables sharing for them.
    static constexpr size_t ADVERT_BLOB_SLOTS = 64;
    struct AdvertBlob {
        uint8_t  key[PUB_KEY_SIZE] = {0};
        uint8_t  data[ADVERT_BLOB_MAX];
        uint16_t len  = 0;
        uint32_t seq  = 0;      // recency counter for LRU eviction
        bool     used = false;
    };
    AdvertBlob  _advertBlobs[ADVERT_BLOB_SLOTS];
    uint32_t    _advertBlobSeq = 0;
    AdvertBlob* findAdvertBlob(const uint8_t* key);   // RAM lookup (nullptr if absent)
    bool        loadAdvertBlobFromSD(const uint8_t* key, uint8_t* dest, uint16_t& len);
    bool        writeAdvertBlobToSD(const uint8_t* key, const uint8_t* data, uint16_t len);
    static void advertBlobPath(const uint8_t* key, char* out, size_t outLen);

    // Callbacks
    MeshMessageCb  _onMessage;
    MeshGroupMsgCb _onGroupMsg;
    MeshAckCb      _onAck;
    MeshFailCb     _onFail;
    MeshAdvertCb    _onAdvert;
    MeshTelemetryCb _onTelemetry;
    MeshTelemetryRawCb _onTelemetryRaw;
    MeshTelemetryRetryCb _onTelemetryRetry;
    MeshAnonRespCb  _onAnonResponse;
    MeshStatusRespCb _onStatusResponse;
    MeshTraceCb     _onTrace;
    MeshRoomMsgCb      _onRoomMsg;
    MeshRoomLoginCb    _onRoomLogin;
    MeshEchoDetectedCb _onEchoDetected;

    // Echo-fingerprint ring: track recently-sent TXT/GRP_TXT payloads so logRxRaw
    // can recognise when a repeater re-broadcasts one of our messages.
    static constexpr int ECHO_SLOTS = 8;
    struct EchoSlot {
        uint32_t fp       = 0;
        uint32_t packetId = 0;
        bool     active   = false;
        bool     fired    = false;  // suppress duplicate callbacks for the same send
    };
    EchoSlot _echoSlots[ECHO_SLOTS] = {};
    uint8_t  _echoIdx = 0;
    // Set by sendDM/sendGroup/sendRoomPost before they call into the base class so
    // sendFloodScoped can associate the payload fingerprint with the right packetId.
    uint32_t _pendingSentPacketId = 0;

    void trackSentFp(uint32_t fp, uint32_t packetId);
    uint32_t        _pendingTelemTag = 0;
    uint8_t         _pendingTelemKey[PUB_KEY_SIZE] = {};
    TelemRetry      _telemRetry;
    uint32_t        _pendingAnonTag = 0;
    uint8_t         _pendingAnonKey[PUB_KEY_SIZE] = {};
    uint32_t        _pendingAnonExpiry = 0;   // millis() deadline; clears the slot if no reply arrives
    uint32_t        _pendingStatusTag = 0;
    uint8_t         _pendingStatusKey[PUB_KEY_SIZE] = {};
    uint32_t        _pendingStatusExpiry = 0; // millis() deadline; clears the slot if no reply arrives

    // Cached BaseChatMesh contact-index per registered room (avoids linear scan
    // on every login/post). Set during begin(); -1 means slot unused.
    static constexpr size_t MAX_ROOMS_RUNTIME = 8;
    int8_t _roomContactIdx[MAX_ROOMS_RUNTIME] = {-1, -1, -1, -1, -1, -1, -1, -1};

    // Per-room scope override, parsed at begin() from cfg.roomServers[i].scope.
    // Null TransportKey = inherit global scope (the default).
    TransportKey _roomScope[MAX_ROOMS_RUNTIME];

    // ACK tracking
    AckEntry _acks[MAX_PENDING_ACKS];
    uint32_t _nextPacketId = 1;
    uint32_t allocPacketId() { uint32_t id = ++_nextPacketId; if (id == 0) id = ++_nextPacketId; return id; }

    CayenneLPP _telemetry;

    AckEntry* findFreeAck();
    AckEntry* findAckByHash(uint32_t hash);
    void checkAckTimeouts();
    void checkTelemTimeout();
    void retryOrFail(AckEntry& entry);
    void _saveIdentity();
};

}  // namespace mclite
