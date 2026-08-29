#pragma once
#include <Arduino.h>

namespace mclite {

// Truncate to at most maxBytes, never splitting a UTF-8 sequence. MeshCore
// truncates over-long text itself, at a raw byte offset -- which can cut a
// multi-byte character in half and leave the receiver storing invalid UTF-8. Any
// path that can exceed the budget should clamp with this first.
inline String truncateUtf8(const String& s, size_t maxBytes) {
    if (s.length() <= maxBytes) return s;
    size_t cut = maxBytes;
    // Walk back off any continuation byte (10xxxxxx) to a lead byte.
    while (cut > 0 && ((uint8_t)s[cut] & 0xC0) == 0x80) cut--;
    return s.substring(0, cut);
}

}  // namespace mclite
