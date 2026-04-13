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

    // Send advertisement (name only, no location)
    bool advertise(const char* name);

    // Send DM — returns internal packetId, 0 on failure
    uint32_t sendDM(size_t contactIdx, const char* text, uint32_t timestamp,
                    uint8_t maxRetries);

    // Send group message — returns true on success
    bool sendGroup(int channelIdx, const char* senderName, const char* text,
                   uint32_t timestamp);

    // Register callbacks
    void onMessage(MeshMessageCb cb)   { _onMessage = cb; }
    void onGroupMsg(MeshGroupMsgCb cb) { _onGroupMsg = cb; }
    void onAck(MeshAckCb cb)           { _onAck = cb; }
    void onFail(MeshFailCb cb)         { _onFail = cb; }
    void onAdvert(MeshAdvertCb cb)     { _onAdvert = cb; }
    void onTelemetry(MeshTelemetryCb cb) { _onTelemetry = cb; }

    // Request telemetry from a contact — returns true on success
    bool requestTelemetry(size_t contactIdx, uint32_t& estTimeout);

    // Clear pending telemetry state (call on timeout)
    void clearPendingTelemetry() { _pendingTelemTag = 0; memset(_pendingTelemKey, 0, PUB_KEY_SIZE); }

    // Access contacts managed by BaseChatMesh
    ContactInfo* getContactByIdx(int idx);

    bool isReady() const { return _ready; }

    // Set configured frequency (must be called before begin())
    void setFrequency(float freq) { _frequency = freq; }

protected:
    // ---- Required BaseChatMesh overrides ----

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

    void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0) override;
    void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0) override;

    // ---- Optional overrides ----
    bool shouldAutoAddContactType(uint8_t type) const override { return false; }  // MCLite uses config-defined contacts only

    // Airtime budget: EU 868-870 MHz → 9.0 (10% duty cycle, ETSI G3),
    //                 otherwise       → 2.0 (33% duty cycle, MeshCore default)
    float getAirtimeBudgetFactor() const override;

private:
    bool _ready = false;
    float _frequency = 0.0f;  // Configured radio frequency (MHz)
    TransportKey _globalScope;  // Derived from RadioConfig::scope at begin()
    void sendWithScope(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis);

    // Callbacks
    MeshMessageCb  _onMessage;
    MeshGroupMsgCb _onGroupMsg;
    MeshAckCb      _onAck;
    MeshFailCb     _onFail;
    MeshAdvertCb    _onAdvert;
    MeshTelemetryCb _onTelemetry;
    uint32_t        _pendingTelemTag = 0;
    uint8_t         _pendingTelemKey[PUB_KEY_SIZE] = {};

    // ACK tracking
    AckEntry _acks[MAX_PENDING_ACKS];
    uint32_t _nextPacketId = 1;
    uint32_t allocPacketId() { uint32_t id = ++_nextPacketId; if (id == 0) id = ++_nextPacketId; return id; }

    CayenneLPP _telemetry;

    AckEntry* findFreeAck();
    AckEntry* findAckByHash(uint32_t hash);
    void checkAckTimeouts();
    void retryOrFail(AckEntry& entry);
    void _saveIdentity();
};

}  // namespace mclite
