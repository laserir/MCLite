#include "ChannelStore.h"
#include "../config/ConfigManager.h"
#include "../util/hex.h"
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <cstring>

namespace mclite {

ChannelStore& ChannelStore::instance() {
    static ChannelStore inst;
    return inst;
}

// Encode raw PSK bytes to base64 string
static String pskToBase64(const uint8_t* psk, size_t len) {
    char b64[64];
    size_t b64Len = 0;
    mbedtls_base64_encode((uint8_t*)b64, sizeof(b64), &b64Len, psk, len);
    return String(b64, b64Len);
}

void ChannelStore::loadFromConfig() {
    _channels.clear();
    const auto& cfg = ConfigManager::instance().config();

    for (const auto& cc : cfg.channels) {
        Channel ch;
        ch.name  = cc.name;
        ch.type  = (cc.type == "private") ? ChannelType::PRIVATE : ChannelType::HASHTAG;
        ch.index   = cc.index;
        ch.allowSos = cc.allowSos;
        ch.sendSos = cc.sendSos;
        ch.readOnly = cc.readOnly;
        ch.scope    = cc.scope;
        memset(ch.psk, 0, sizeof(ch.psk));

        if (cc.psk.length() > 0) {
            // Decode PSK — supports hex (32 or 64 chars) and base64 (legacy)
            bool pskOk = false;
            size_t outLen = 0;

            if ((cc.psk.length() == 32 || cc.psk.length() == 64) && isHexString(cc.psk)) {
                // Hex format
                outLen = cc.psk.length() / 2;
                for (size_t i = 0; i < outLen; i++) {
                    char byte[3] = { cc.psk[i*2], cc.psk[i*2+1], 0 };
                    ch.psk[i] = (uint8_t)strtoul(byte, nullptr, 16);
                }
                pskOk = true;
            } else {
                // Base64 format (legacy)
                int ret = mbedtls_base64_decode(ch.psk, sizeof(ch.psk), &outLen,
                                      (const uint8_t*)cc.psk.c_str(),
                                      cc.psk.length());
                pskOk = (ret == 0 && (outLen == 16 || outLen == 32));
            }

            if (!pskOk) {
                Serial.printf("[ChannelStore] Skipping channel '%s': invalid PSK\n",
                              cc.name.c_str());
                continue;
            }
            ch.pskLen = (uint8_t)outLen;
            ch.pskB64 = pskToBase64(ch.psk, outLen);
        } else if (cc.name.length() > 0 && cc.name[0] == '#') {
            // Hashtag channel: derive PSK as SHA256(name)[:16]
            // Name includes the '#' prefix, matching MeshCore convention
            // Sanitize: lowercase + strip invalid chars (only a-z, 0-9, - allowed after #).
            // MeshCore official apps only allow lowercase hashtag names.
            // If a future MeshCore version allows uppercase, remove this normalization.
            String normalized;
            for (size_t j = 0; j < cc.name.length(); j++) {
                char c = tolower(cc.name[j]);
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '#')
                    normalized += c;
            }
            if (normalized.length() <= 1) {  // just '#' or empty
                Serial.printf("[ChannelStore] Skipping hashtag channel '%s': invalid name\n", cc.name.c_str());
                continue;
            }
            ch.name = normalized;
            uint8_t hash[32];
            mbedtls_sha256((const uint8_t*)normalized.c_str(), normalized.length(), hash, 0);
            memcpy(ch.psk, hash, 16);
            memset(ch.psk + 16, 0, 16);
            ch.pskLen = 16;
            ch.pskB64 = pskToBase64(ch.psk, 16);
            Serial.printf("[ChannelStore] Derived PSK for hashtag channel '%s'\n", cc.name.c_str());
        } else {
            Serial.printf("[ChannelStore] Skipping channel '%s': no PSK provided\n", cc.name.c_str());
            continue;
        }

        _channels.push_back(ch);
    }

    Serial.printf("[ChannelStore] Loaded %u channels\n", (unsigned)_channels.size());
}

void ChannelStore::addChannel(const Channel& ch) {
    _channels.push_back(ch);
}

Channel* ChannelStore::findByName(const String& name) {
    for (auto& ch : _channels) {
        if (ch.name == name) return &ch;
    }
    return nullptr;
}

Channel* ChannelStore::findByIndex(uint8_t index) {
    for (auto& ch : _channels) {
        if (ch.index == index) return &ch;
    }
    return nullptr;
}

}  // namespace mclite
