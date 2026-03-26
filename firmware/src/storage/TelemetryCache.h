#pragma once

#include <cstdint>
#include <cstring>

// MAX_CONTACTS defined as build flag in platformio.ini

namespace mclite {

struct TelemetryData {
    float    voltage    = 0;
    bool     hasVoltage = false;
    double   lat = 0, lon = 0, alt = 0;
    bool     hasLocation = false;
    float    temperature = 0;      // °C
    bool     hasTemperature = false;
    float    humidity    = 0;      // %
    bool     hasHumidity = false;
    float    pressure    = 0;      // hPa
    bool     hasPressure = false;
    uint32_t receivedAt = 0;  // millis() when received
};

class TelemetryCache {
public:
    static constexpr uint32_t STALE_MS = 30UL * 60 * 1000;  // 30 minutes

    void store(const uint8_t* pubKey32, const TelemetryData& data) {
        // Overwrite existing entry if found
        for (int i = 0; i < _count; i++) {
            if (memcmp(_keys[i], pubKey32, 32) == 0) {
                _data[i] = data;
                return;
            }
        }
        // Add new entry (evict oldest if full)
        int slot = _count < MAX_CONTACTS ? _count++ : findOldest();
        memcpy(_keys[slot], pubKey32, 32);
        _data[slot] = data;
    }

    const TelemetryData* get(const uint8_t* pubKey32) const {
        for (int i = 0; i < _count; i++) {
            if (memcmp(_keys[i], pubKey32, 32) == 0) {
                return &_data[i];
            }
        }
        return nullptr;
    }

    bool isFresh(const uint8_t* pubKey32) const {
        const TelemetryData* d = get(pubKey32);
        return d && (millis() - d->receivedAt < STALE_MS);
    }

    static TelemetryCache& instance() {
        static TelemetryCache inst;
        return inst;
    }

private:
    TelemetryCache() = default;

    int findOldest() const {
        int oldest = 0;
        for (int i = 1; i < _count; i++) {
            if ((int32_t)(_data[i].receivedAt - _data[oldest].receivedAt) < 0) {
                oldest = i;
            }
        }
        return oldest;
    }

    uint8_t       _keys[MAX_CONTACTS][32] = {};
    TelemetryData _data[MAX_CONTACTS];
    int           _count = 0;
};

}  // namespace mclite
