#pragma once

#include <vector>
#include <cstdint>
#include <Arduino.h>

namespace mclite {

enum class ChannelType : uint8_t {
    HASHTAG,   // Open group channel (#)
    PRIVATE    // PSK-protected channel (lock icon)
};

struct Channel {
    String      name;
    ChannelType type;
    uint8_t     psk[32];     // Pre-shared key bytes (16 or 32, zero-padded)
    uint8_t     pskLen = 0;  // Actual key length (16 or 32)
    String      pskB64;      // Base64 encoded (what gets passed to MeshCore)
    uint8_t     index;       // Channel index in MeshCore

    bool    allowSos = true;  // Allow SOS alerts from this channel
    bool    sendSos = true;   // Include in outgoing SOS broadcast
    bool    readOnly = false;  // Hide input bar in chat view

    bool isPrivate() const { return type == ChannelType::PRIVATE; }
};

class ChannelStore {
public:
    void loadFromConfig();
    void addChannel(const Channel& ch);

    Channel* findByName(const String& name);
    Channel* findByIndex(uint8_t index);

    size_t count() const { return _channels.size(); }
    const std::vector<Channel>& all() const { return _channels; }

    static ChannelStore& instance();

private:
    ChannelStore() = default;
    std::vector<Channel> _channels;
};

}  // namespace mclite
