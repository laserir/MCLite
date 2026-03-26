#pragma once

#include <vector>
#include <cstdint>
#include <Arduino.h>

namespace mclite {

struct Contact {
    String  name;
    uint8_t publicKey[32];   // Raw public key bytes
    String  publicKeyB64;    // Base64 encoded
    bool    online  = false;
    uint32_t lastSeen = 0;   // millis() of last message/advert
    bool    allowTelemetry = true;   // Base telemetry permission (must be true for location/environment to work)
    bool    allowLocation  = false;  // Respond to GPS location requests (requires allowTelemetry)
    bool    allowEnvironment = false;  // Respond to environment sensor requests (requires allowTelemetry)
    bool    alwaysSound    = false;  // Play notification even when muted
    bool    allowSos       = true;   // Allow SOS alerts from this contact
    bool    sendSos        = true;   // Include in outgoing SOS broadcast

    // Short hex ID for file paths (first 8 chars of hex-encoded pubkey)
    const String& shortId() const { return _shortId; }
    void computeShortId();  // Call after publicKey is set

private:
    String _shortId;
};

class ContactStore {
public:
    void loadFromConfig();

    Contact* findByName(const String& name);
    Contact* findByPublicKey(const uint8_t* key);
    Contact* findByIndex(size_t index);

    size_t count() const { return _contacts.size(); }
    const std::vector<Contact>& all() const { return _contacts; }

    void updateLastSeen(const uint8_t* key);

    static ContactStore& instance();

private:
    ContactStore() = default;
    std::vector<Contact> _contacts;
};

}  // namespace mclite
