#include "TimeHelper.h"
#include "../config/ConfigManager.h"
#include <sys/time.h>
#include <Arduino.h>

namespace mclite {

bool TimeHelper::isValidPosixTz(const String& tz) {
    bool hasAlpha = false, hasDigit = false;
    for (size_t i = 0; i < tz.length(); i++) {
        char c = tz[i];
        if (isalpha(c)) hasAlpha = true;
        if (isdigit(c)) { hasDigit = true; break; }
    }
    return hasAlpha && hasDigit;
}

void TimeHelper::applyTimezone() {
    const auto& cfg = ConfigManager::instance().config();

    String tz;
    if (cfg.gpsTimezone.length() > 0) {
        if (isValidPosixTz(cfg.gpsTimezone)) {
            tz = cfg.gpsTimezone;
        } else {
            Serial.printf("[Time] Invalid timezone string: \"%s\" — ignoring, using offset fallback\n",
                          cfg.gpsTimezone.c_str());
        }
    }
    if (tz.length() == 0) {
        if (cfg.gpsClockOffset != 0) {
            // Backward compat: convert static offset to POSIX TZ string.
            // POSIX convention: positive = west of UTC (inverted from ISO 8601).
            // gpsClockOffset +1 (east, e.g. CET) → "UTC-1"
            int inv = -cfg.gpsClockOffset;
            tz = "UTC" + String(inv);
        } else {
            tz = "UTC0";
        }
    }

    setenv("TZ", tz.c_str(), 1);
    tzset();
    Serial.printf("[Time] Timezone: %s\n", tz.c_str());
}

void TimeHelper::syncSystemClock(uint32_t utcEpoch) {
    if (utcEpoch < 1700000000) return;
    if (utcEpoch == _lastSyncEpoch) return;

    struct timeval tv;
    tv.tv_sec = utcEpoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    _lastSyncEpoch = utcEpoch;
    _synced = true;
}

void TimeHelper::formatHHMM(uint32_t utcEpoch, char* buf, size_t bufLen) const {
    if (utcEpoch < 1700000000 || bufLen < 6) {
        buf[0] = '\0';
        return;
    }
    time_t t = (time_t)utcEpoch;
    struct tm result;
    localtime_r(&t, &result);
    snprintf(buf, bufLen, "%02d:%02d", result.tm_hour, result.tm_min);
}

}  // namespace mclite
