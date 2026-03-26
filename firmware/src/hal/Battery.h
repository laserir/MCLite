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

    static Battery& instance();

private:
    Battery() = default;
    uint16_t _mv  = 0;
    uint8_t  _pct = 0;
    bool _charging = false;
    uint32_t _lastRead = 0;
    static constexpr uint32_t READ_INTERVAL_MS = 5000;
};

}  // namespace mclite
