#pragma once
#include <Arduino.h>
#include <mbedtls/sha256.h>

namespace mclite {

// Compute the 8-char Crockford Base32 message hash per the MeshCore Reactions spec:
//   SHA-256( UTF-8(text) || LE32(senderTimestamp) ) → first 5 bytes → Crockford B32
// Use the sender's original timestamp, not local receive time, so all nodes agree.
inline String computeMsgHash(const String& text, uint32_t senderTimestamp) {
    static constexpr const char CROCKFORD[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    // Build input: text bytes + 4-byte little-endian timestamp.
    // Messages are capped at MAX_MSG_BYTES (160), so 164 bytes is a safe stack bound.
    static constexpr size_t BUF_MAX = 164;
    uint8_t buf[BUF_MAX];
    const size_t tlen = text.length() < BUF_MAX - 4 ? text.length() : BUF_MAX - 4;
    memcpy(buf, text.c_str(), tlen);
    buf[tlen]     = (uint8_t)(senderTimestamp);
    buf[tlen + 1] = (uint8_t)(senderTimestamp >> 8);
    buf[tlen + 2] = (uint8_t)(senderTimestamp >> 16);
    buf[tlen + 3] = (uint8_t)(senderTimestamp >> 24);

    uint8_t hash[32];
    mbedtls_sha256(buf, tlen + 4, hash, 0);  // 0 = SHA-256

    // Encode first 5 bytes (40 bits) into 8 Crockford B32 chars (5 bits each)
    uint64_t bits = ((uint64_t)hash[0] << 32) |
                    ((uint64_t)hash[1] << 24) |
                    ((uint64_t)hash[2] << 16) |
                    ((uint64_t)hash[3] <<  8) |
                     (uint64_t)hash[4];
    char result[9];
    for (int i = 7; i >= 0; i--) {
        result[i] = CROCKFORD[bits & 0x1F];
        bits >>= 5;
    }
    result[8] = '\0';
    return String(result);
}

// True if s is a valid 8-char Crockford B32 string.
// Case-insensitive; normalizes O→0, I/L→1 per spec.
inline bool isCrockfordB32(const String& s) {
    if (s.length() != 8) return false;
    for (size_t i = 0; i < 8; i++) {
        char c = (char)toupper((unsigned char)s[i]);
        if (c == 'O') c = '0';
        else if (c == 'I' || c == 'L') c = '1';
        bool ok = (c >= '0' && c <= '9') ||
                  (c >= 'A' && c <= 'H') ||
                  c == 'J' || c == 'K' ||
                  c == 'M' || c == 'N' ||
                  (c >= 'P' && c <= 'T') ||
                  (c >= 'V' && c <= 'Z');
        if (!ok) return false;
    }
    return true;
}

// Normalize to canonical uppercase Crockford form.
inline String normalizeCrockford(const String& s) {
    String up = s;
    up.toUpperCase();
    char buf[9];
    size_t len = up.length() < 8 ? up.length() : 8;
    memcpy(buf, up.c_str(), len);
    buf[len] = '\0';
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == 'O') buf[i] = '0';
        else if (buf[i] == 'I' || buf[i] == 'L') buf[i] = '1';
    }
    return String(buf);
}

}  // namespace mclite
