#pragma once

#include <cstdint>
#include <cstring>
#include <Arduino.h>

namespace mclite {

constexpr int HEARD_ADVERT_CAP = 64;

// Path buffer matches MeshCore's MAX_PATH_SIZE — large enough for any legal advert
// regardless of hash-size mode (1..4 bytes per hop, 0..63 hops).
constexpr int HEARD_PATH_CAP = 64;

struct HeardAdvert {
    uint8_t  pubKey[32]               = {};
    char     name[32]                 = {};
    uint8_t  type                     = 0;        // ADV_TYPE_* (CHAT=1, REPEATER=2, ROOM=3, SENSOR=4)
    uint8_t  hops                     = 0;        // hop count (0..63); 0 = direct
    uint8_t  hashSize                 = 1;        // bytes per hop hash (1..4); derived from packed path_len
    uint8_t  pathByteLen              = 0;        // bytes of pathBytes that are valid (0..HEARD_PATH_CAP)
    uint8_t  pathBytes[HEARD_PATH_CAP] = {};
    int32_t  gpsLat                   = 0;        // 6 dec places (matches MeshCore ContactInfo)
    int32_t  gpsLon                   = 0;
    bool     hasGps                   = false;
    uint32_t lastHeardMs              = 0;        // millis() at last reception
    // TODO: SNR (requires onAdvertRecv override to access mesh::Packet)
};

class HeardAdvertCache {
public:
    // packedPathLen is the raw MeshCore Packet::path_len byte
    // (low 6 bits = hop count, upper 2 bits encode the per-hop hash size: 1..4 bytes).
    // pathBytes may be nullptr when hops == 0; otherwise it points to hops*hashSize bytes.
    void store(const uint8_t* pubKey32,
               const char* name,
               uint8_t type,
               uint8_t packedPathLen,
               const uint8_t* pathBytes,
               int32_t gpsLat,
               int32_t gpsLon) {
        const uint32_t now = millis();
        const bool hasGps = (gpsLat != 0 || gpsLon != 0);
        const uint8_t hops     = packedPathLen & 0x3F;
        const uint8_t hashSize = (packedPathLen >> 6) + 1;
        const uint16_t want    = (uint16_t)hops * hashSize;
        const uint8_t  copyLen = (uint8_t)(want > HEARD_PATH_CAP ? HEARD_PATH_CAP : want);

        for (int i = 0; i < _count; i++) {
            if (memcmp(_entries[i].pubKey, pubKey32, 32) == 0) {
                fill(_entries[i], pubKey32, name, type, hops, hashSize,
                     pathBytes, copyLen, gpsLat, gpsLon, hasGps, now);
                _version++;
                return;
            }
        }

        int slot = _count < HEARD_ADVERT_CAP ? _count++ : findOldest();
        fill(_entries[slot], pubKey32, name, type, hops, hashSize,
             pathBytes, copyLen, gpsLat, gpsLon, hasGps, now);
        _version++;
    }

    int count() const { return _count; }

    const HeardAdvert* entries() const { return _entries; }

    // Bumped on every store() or clear(). Screens can poll to detect changes.
    uint32_t version() const { return _version; }

    // Wipe all entries. Bumps version so live UIs refresh to the empty state.
    void clear() {
        _count = 0;
        _version++;
    }

    static HeardAdvertCache& instance() {
        static HeardAdvertCache inst;
        return inst;
    }

private:
    HeardAdvertCache() = default;

    static void fill(HeardAdvert& e,
                     const uint8_t* pubKey32, const char* name,
                     uint8_t type, uint8_t hops, uint8_t hashSize,
                     const uint8_t* pathBytes, uint8_t pathByteLen,
                     int32_t gpsLat, int32_t gpsLon, bool hasGps,
                     uint32_t now) {
        memcpy(e.pubKey, pubKey32, 32);
        if (name) {
            strncpy(e.name, name, sizeof(e.name) - 1);
            e.name[sizeof(e.name) - 1] = '\0';
        } else {
            e.name[0] = '\0';
        }
        e.type        = type;
        e.hops        = hops;
        e.hashSize    = hashSize;
        e.pathByteLen = pathByteLen;
        if (pathBytes && pathByteLen > 0) {
            memcpy(e.pathBytes, pathBytes, pathByteLen);
        }
        // Zero unused tail so debug dumps don't show stale bytes
        if (pathByteLen < HEARD_PATH_CAP) {
            memset(e.pathBytes + pathByteLen, 0, HEARD_PATH_CAP - pathByteLen);
        }
        e.gpsLat      = gpsLat;
        e.gpsLon      = gpsLon;
        e.hasGps      = hasGps;
        e.lastHeardMs = now;
    }

    int findOldest() const {
        int oldest = 0;
        for (int i = 1; i < _count; i++) {
            // Wrap-safe comparison
            if ((int32_t)(_entries[i].lastHeardMs - _entries[oldest].lastHeardMs) < 0) {
                oldest = i;
            } else if (_entries[i].lastHeardMs == _entries[oldest].lastHeardMs &&
                       _entries[i].pubKey[0] < _entries[oldest].pubKey[0]) {
                // Deterministic tie-break by lowest pubkey first byte
                oldest = i;
            }
        }
        return oldest;
    }

    HeardAdvert _entries[HEARD_ADVERT_CAP];
    int         _count   = 0;
    uint32_t    _version = 0;
};

}  // namespace mclite
