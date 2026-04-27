#pragma once

#include <Arduino.h>

namespace mclite {

inline bool isHexString(const String& s) {
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return s.length() > 0;
}

// Render the first 8 bytes of a public key as a 16-char lowercase hex string.
// Matches Contact::computeShortId — used for ConvoId{ROOM, shortId} and DM ids.
inline String pubKeyToShortId(const uint8_t* key) {
    char hex[17];
    for (int i = 0; i < 8; i++) sprintf(hex + i * 2, "%02x", key[i]);
    hex[16] = '\0';
    return String(hex);
}

}  // namespace mclite
