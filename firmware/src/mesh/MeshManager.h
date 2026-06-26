#pragma once

#include <cstdint>
#include <functional>
#include <Arduino.h>
#include "../storage/TelemetryCache.h"
#include "../config/defaults.h"

namespace mclite {

class MCLiteMesh;  // Forward declaration

// Callback types for mesh events (used by main.cpp / UIManager)
using OnMessageCallback  = std::function<void(const String& senderName,
                                               const uint8_t* senderKey,
                                               const String& text,
                                               uint32_t timestamp,
                                               uint8_t hops)>;
using OnGroupMsgCallback = std::function<void(uint8_t channelIdx,
                                               const String& senderName,
                                               const String& text,
                                               uint32_t timestamp,
                                               uint8_t hops)>;
using OnAckCallback      = std::function<void(uint32_t packetId)>;
using OnFailCallback     = std::function<void(uint32_t packetId)>;
using OnAdvertCallback     = std::function<void(const uint8_t* senderKey)>;
using OnTelemetryCallback  = std::function<void(const uint8_t* pubKey, const TelemetryData& data)>;
using OnTelemetryRawCallback = std::function<void(const uint8_t* pubKey, const uint8_t* lpp, uint8_t lppLen)>;
using OnTelemetryRetryCallback = std::function<void(uint32_t newTimeoutMs)>;
using OnAnonResponseCallback = std::function<void(uint32_t tag, const uint8_t* data, uint8_t len)>;
using OnStatusResponseCallback = std::function<void(const uint8_t* pubKey, const uint8_t* data, uint8_t len)>;
using OnTraceCallback = std::function<void(uint32_t tag, uint32_t auth, uint8_t flags,
                                           const uint8_t* path_snrs, const uint8_t* path_hashes,
                                           uint8_t path_len, int8_t final_snr)>;
using OnRoomMessageCallback = std::function<void(size_t roomIdx,
                                                  const String& roomName,
                                                  const uint8_t* senderPrefix /* 4 B */,
                                                  const String& text,
                                                  uint32_t timestamp)>;
using OnRoomLoginCallback   = std::function<void(size_t roomIdx,
                                                  const String& roomName,
                                                  uint8_t status,
                                                  uint8_t permissions,
                                                  uint8_t aclPerms,
                                                  uint8_t fwLevel)>;
using OnEchoDetectedCallback = std::function<void(uint32_t packetId)>;

class MeshManager {
public:
    bool init();         // Initialize radio + mesh
    void update();       // Call from main loop (processes radio events)

    // Send a DM to a contact by index — returns internal packet ID
    uint32_t sendMessage(size_t contactIndex, const String& text);

    // Send a group message to a channel by index — returns internal packet ID
    uint32_t sendGroupMessage(uint8_t channelIndex, const String& text);

    // Set callbacks
    void onMessage(OnMessageCallback cb)      { _onMessage = cb; }
    void onGroupMessage(OnGroupMsgCallback cb) { _onGroupMsg = cb; }
    void onAck(OnAckCallback cb)              { _onAck = cb; }
    void onFail(OnFailCallback cb)            { _onFail = cb; }
    void onAdvert(OnAdvertCallback cb)        { _onAdvert = cb; }
    void onTelemetry(OnTelemetryCallback cb)  { _onTelemetry = cb; }
    void onTelemetryRaw(OnTelemetryRawCallback cb) { _onTelemetryRaw = cb; }
    void onTelemetryRetry(OnTelemetryRetryCallback cb) { _onTelemetryRetry = cb; }
    void onAnonResponse(OnAnonResponseCallback cb) { _onAnonResponse = cb; }
    void onStatusResponse(OnStatusResponseCallback cb) { _onStatusResponse = cb; }
    void onTrace(OnTraceCallback cb) { _onTrace = cb; }
    void onRoomMessage(OnRoomMessageCallback cb)   { _onRoomMsg = cb; }
    void onRoomLogin(OnRoomLoginCallback cb)       { _onRoomLogin = cb; }
    void onEchoDetected(OnEchoDetectedCallback cb) { _onEchoDetected = cb; }

    // Login to a configured room server (by config index 0..7). Returns true on
    // a successful send; the actual login outcome arrives asynchronously via
    // onRoomLogin. Idempotent server-side (refreshes the session).
    bool loginRoom(size_t roomIdx, uint32_t& estTimeout);
    // Same, but with an explicit password (used by the companion app, which
    // supplies its own; not persisted). Falls back to the config-password overload
    // by leaving the app field empty — see CompanionService::cmdSendLogin.
    bool loginRoom(size_t roomIdx, const char* password, uint32_t& estTimeout);

    // Send a post to a room (by config index). Returns internal packetId, 0 on
    // failure. ACK arrives via the existing onAck/onFail callbacks.
    uint32_t sendRoomPost(size_t roomIdx, const String& text);

    // Request telemetry from a contact — returns true on success
    bool requestTelemetry(size_t contactIndex, uint32_t& estTimeout);
    // Same, addressed by 32-byte pubkey (companion app path).
    bool requestTelemetryByKey(const uint8_t* pubKey, uint32_t& estTimeout);

    // Send an anonymous request to a node by pubkey (companion app path). Fills
    // tag + estTimeout; the reply arrives via the onAnonResponse callback.
    bool sendAnonReqByKey(const uint8_t* pubKey, const uint8_t* data, uint8_t len,
                          uint32_t& tag, uint32_t& estTimeout);
    // True while an anonymous request is awaiting a reply (single-slot).
    bool isAnonReqPending() const;

    // Send a status request to a known contact by pubkey; reply via onStatusResponse.
    bool sendStatusReqByKey(const uint8_t* pubKey, uint32_t& tag, uint32_t& estTimeout);
    bool isStatusReqPending() const;
    // Send a path trace along an explicit path; reply via onTrace.
    bool sendTracePath(uint32_t tag, uint32_t auth, uint8_t flags,
                       const uint8_t* path, uint8_t path_len, uint32_t& estTimeout);

    // ─── Contact sharing ───
    // Re-broadcast a contact's signed advert at zero hop so nearby nodes can add
    // it (lands in their Heard Adverts). Returns false if no advert blob is held
    // or the radio isn't ready.
    bool shareContact(const uint8_t* pubKey);
    // True if a re-broadcastable advert blob exists for this pubkey (RAM or SD).
    bool canShareContact(const uint8_t* pubKey) const;
    // Persist a just-saved contact's advert blob so Share survives a reboot.
    bool persistAdvertBlob(const uint8_t* pubKey);
    // Drop a removed contact's persisted advert blob.
    void deleteAdvertBlob(const uint8_t* pubKey);
    // Reset a contact's learned path (forces flood rediscovery). Returns false if unknown.
    bool resetPathByKey(const uint8_t* pubKey);

    // Clear pending telemetry state (call on timeout)
    void clearPendingTelemetry();

    // True while a telemetry request (UI or auto) is awaiting a reply.
    bool isTelemetryPending() const;

    bool isRadioReady() const { return _radioReady; }

    // This node's 32-byte public key, or nullptr before the mesh is initialized.
    const uint8_t* selfPubKey() const;

    // Direct access to the underlying mesh for read-only companion queries
    // (contacts/channels/RTC). nullptr before init(). Use sparingly.
    MCLiteMesh* mesh() const { return _mesh; }

    // TX duty cycle over the last hour (0.0–100.0%)
    float getTxDutyCyclePercent() const;

    // True if configured frequency is in the EU 868–870 MHz band
    bool isEURegion() const;

    // Periodic advertisement interval (ms). 0 = disabled.
    void setAdvertInterval(uint32_t ms) { _advertIntervalMs = ms; }

    // Send an advertisement immediately (user-triggered) and reset the
    // periodic timer so the next scheduled advert is a full interval out.
    // flood = true (default) is mesh-wide — the on-device button; flood = false
    // is zero-hop/local for the companion app's local-advert option.
    // Returns the underlying mesh send result.
    bool sendAdvertNow(bool flood = true);

    static MeshManager& instance();

private:
    MeshManager() = default;

    bool _radioReady = false;

    OnMessageCallback  _onMessage;
    OnGroupMsgCallback _onGroupMsg;
    OnAckCallback      _onAck;
    OnFailCallback     _onFail;
    OnAdvertCallback    _onAdvert;
    OnTelemetryCallback _onTelemetry;
    OnTelemetryRawCallback _onTelemetryRaw;
    OnTelemetryRetryCallback _onTelemetryRetry;
    OnAnonResponseCallback _onAnonResponse;
    OnStatusResponseCallback _onStatusResponse;
    OnTraceCallback _onTrace;
    OnRoomMessageCallback  _onRoomMsg;
    OnRoomLoginCallback    _onRoomLogin;
    OnEchoDetectedCallback _onEchoDetected;

    // Advertisement
    uint32_t _advertIntervalMs = 0;  // Periodic flood-advert interval (ms). 0 = off (default; boot advert
                                     // only). Set from radio.advert_interval_min — see issue #13.
    uint32_t _lastAdvertMs     = 0;
    bool     _firstAdvert      = true;   // Send first advert immediately on boot

    // The MeshCore mesh instance
    MCLiteMesh* _mesh = nullptr;

    // Rolling 1-hour TX duty cycle tracking
    static constexpr uint8_t DC_SLOTS = 60;
    uint32_t _dcDeltas[DC_SLOTS] = {};
    uint32_t _dcLastSample   = 0;
    uint32_t _dcLastSampleMs = 0;
    uint8_t  _dcSlotIdx      = 0;
    uint8_t  _dcSlotsFilled  = 0;

    // Auto-refresh contact GPS (background telemetry). Session-only state.
    void     tickAutoTelemetry();
    uint32_t _autoTelemLastScanMs     = 0;
    size_t   _autoTelemNextIdx        = 0;   // round-robin cursor over contacts
    int      _autoTelemAwaitIdx       = -1;  // contact idx of the in-flight auto request, -1 = none
    uint32_t _autoTelemAwaitSentMs    = 0;
    uint32_t _autoTelemAwaitDeadlineMs = 0;
    uint8_t  _autoTelemMisses[defaults::MAX_CHAT_CONTACTS] = {};  // consecutive misses per contact (back-off)

    bool initRadio();
    void wireCallbacks();
};

}  // namespace mclite
