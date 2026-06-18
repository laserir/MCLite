#pragma once

#include <cstdint>

namespace mclite {

class Battery {
public:
    void init();
    void update();  // Call periodically to read ADC

    uint16_t milliVolts() const { return _mv; }
    uint8_t  percent() const    { return _pct; }
    bool     isCharging() const { return _charging; }

    // Timestamp (bestEpoch) and battery % when charging last stopped.
    // In-memory only; resets on reboot. 0 = not yet recorded this boot.
    uint32_t lastChargedEpoch() const { return _lastChargedEpoch; }
    uint8_t  lastChargedPercent() const { return _lastChargedPct; }

    static Battery& instance();

private:
    Battery() = default;
    uint16_t _mv  = 0;
    uint8_t  _pct = 0;
    bool _charging = false;
    bool _prevCharging = false;   // for edge detection
    uint32_t _lastRead = 0;
    uint32_t _lastChargedEpoch = 0;
    uint8_t  _lastChargedPct = 0;
    static constexpr uint32_t READ_INTERVAL_MS = 5000;
};

}  // namespace mclite
