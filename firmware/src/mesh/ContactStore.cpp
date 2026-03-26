#include "ContactStore.h"
#include "../config/ConfigManager.h"
#include "../util/hex.h"
#include <mbedtls/base64.h>
#include <cstring>

namespace mclite {

ContactStore& ContactStore::instance() {
    static ContactStore inst;
    return inst;
}

void Contact::computeShortId() {
    char hex[17];
    for (int i = 0; i < 8; i++) {
        sprintf(hex + i * 2, "%02x", publicKey[i]);
    }
    hex[16] = '\0';
    _shortId = hex;
}

void ContactStore::loadFromConfig() {
    _contacts.clear();
    const auto& cfg = ConfigManager::instance().config();

    for (const auto& cc : cfg.contacts) {
        Contact c;
        c.name = cc.alias;
        c.publicKeyB64 = cc.publicKey;
        c.allowTelemetry = cc.allowTelemetry;
        c.allowLocation  = cc.allowLocation;
        c.allowEnvironment = cc.allowEnvironment;
        c.alwaysSound    = cc.alwaysSound;
        c.allowSos       = cc.allowSos;
        c.sendSos        = cc.sendSos;

        // Decode public key — supports both hex (64 chars) and base64
        bool keyOk = false;
        if (cc.publicKey.length() == 64 && isHexString(cc.publicKey)) {
            // Hex format
            for (int i = 0; i < 32; i++) {
                char byte[3] = { cc.publicKey[i*2], cc.publicKey[i*2+1], 0 };
                c.publicKey[i] = (uint8_t)strtoul(byte, nullptr, 16);
            }
            keyOk = true;
        } else {
            // Base64 format
            size_t outLen = 0;
            int ret = mbedtls_base64_decode(c.publicKey, sizeof(c.publicKey), &outLen,
                                  (const uint8_t*)cc.publicKey.c_str(),
                                  cc.publicKey.length());
            keyOk = (ret == 0 && outLen == 32);
        }

        if (!keyOk) {
            Serial.printf("[ContactStore] Skipping contact '%s': invalid public key\n",
                          cc.alias.c_str());
            continue;
        }

        c.computeShortId();
        _contacts.push_back(c);
    }

    Serial.printf("[ContactStore] Loaded %u contacts\n", (unsigned)_contacts.size());
}

Contact* ContactStore::findByName(const String& name) {
    for (auto& c : _contacts) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

Contact* ContactStore::findByPublicKey(const uint8_t* key) {
    for (auto& c : _contacts) {
        if (memcmp(c.publicKey, key, 32) == 0) return &c;
    }
    return nullptr;
}

Contact* ContactStore::findByIndex(size_t index) {
    if (index < _contacts.size()) return &_contacts[index];
    return nullptr;
}

void ContactStore::updateLastSeen(const uint8_t* key) {
    Contact* c = findByPublicKey(key);
    if (c) {
        c->lastSeen = millis();
        c->online = true;
    }
}

}  // namespace mclite
