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

}  // namespace mclite
