#pragma once

#include <cstdint>
#include <functional>
#include <Arduino.h>
#include "../storage/TelemetryCache.h"

namespace mclite {

class MCLiteMesh;  // Forward declaration

// Callback types for mesh events (used by main.cpp / UIManager)
using OnMessageCallback  = std::function<void(const String& senderName,
                                               const uint8_t* senderKey,
                                               const String& text,
                                               uint32_t timestamp)>;
using OnGroupMsgCallback = std::function<void(uint8_t channelIdx,
                                               const String& senderName,
                                               const String& text,
                                               uint32_t timestamp)>;
using OnAckCallback      = std::function<void(uint32_t packetId)>;
using OnFailCallback     = std::function<void(uint32_t packetId)>;
using OnAdvertCallback     = std::function<void(const uint8_t* senderKey)>;
using OnTelemetryCallback  = std::function<void(const uint8_t* pubKey, const TelemetryData& data)>;

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

    // Request telemetry from a contact — returns true on success
    bool requestTelemetry(size_t contactIndex, uint32_t& estTimeout);

    // Clear pending telemetry state (call on timeout)
    void clearPendingTelemetry();

    bool isRadioReady() const { return _radioReady; }

    // TX duty cycle over the last hour (0.0–100.0%)
    float getTxDutyCyclePercent() const;

    // True if configured frequency is in the EU 868–870 MHz band
    bool isEURegion() const;

    // Periodic advertisement interval (ms). 0 = disabled.
    void setAdvertInterval(uint32_t ms) { _advertIntervalMs = ms; }

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

    // Advertisement
    uint32_t _advertIntervalMs = 300000;  // Default: every 5 minutes
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

    bool initRadio();
    void wireCallbacks();
};

}  // namespace mclite
