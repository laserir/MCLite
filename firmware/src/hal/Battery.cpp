#include "Battery.h"
#include <Arduino.h>

namespace mclite {

Battery& Battery::instance() {
    static Battery inst;
    return inst;
}

void Battery::init() {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    update();
}

void Battery::update() {
    uint32_t now = millis();
    if (now - _lastRead < READ_INTERVAL_MS && _lastRead != 0) return;
    _lastRead = now;

    // T-Deck Plus: battery voltage on ADC pin with 2:1 divider
    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
        raw += analogReadMilliVolts(TDECK_BAT_ADC);
    }
    raw /= 8;

    // 2:1 voltage divider
    _mv = raw * 2;

    // Linear approximation: 3000mV = 0%, 4200mV = 100%
    if (_mv >= 4200) _pct = 100;
    else if (_mv <= 3000) _pct = 0;
    else _pct = (uint8_t)(((uint32_t)(_mv - 3000) * 100) / 1200);

    _charging = (_mv > 4250);  // Rough charging detection
}

}  // namespace mclite
