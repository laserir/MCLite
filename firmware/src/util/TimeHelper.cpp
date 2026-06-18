#include "TimeHelper.h"
#include "util/log.h"
#include "../i18n/I18n.h"
#include "../config/ConfigManager.h"
#include "../net/WiFiManager.h"
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
            LOGF("[Time] Invalid timezone string: \"%s\" — ignoring, using offset fallback\n",
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

    _tz = tz;
    setenv("TZ", tz.c_str(), 1);
    tzset();
    LOGF("[Time] Timezone: %s\n", tz.c_str());
}

void TimeHelper::maybeNtpSync() {
    if (_synced) return;                                   // GPS or a prior NTP already set the clock
    if (!WiFiManager::instance().isConnected()) {
        _ntpStarted = false;                              // re-arm for the next connection
        return;
    }
    if (!_ntpStarted) {
        // Start SNTP once per connection (also (re)applies the TZ). Non-blocking.
        const char* tz = _tz.length() ? _tz.c_str() : "UTC0";
        configTzTime(tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
        _ntpStarted = true;
        LOGF("[Time] NTP started (TZ=%s)\n", tz);
        return;
    }
    // Poll non-blocking; getLocalTime() returns true only once the clock is set.
    struct tm tmv;
    if (getLocalTime(&tmv, 0)) {
        time_t now = time(nullptr);
        if (now > 1700000000) {
            syncSystemClock((uint32_t)now);
            LOGF("[Time] NTP synced: %lu\n", (unsigned long)now);
        }
    }
}

uint32_t TimeHelper::nowEpoch() const {
    if (!_synced) return 0;
    return (uint32_t)time(nullptr);
}

uint32_t TimeHelper::bestEpoch() const {
    uint32_t e = nowEpoch();
    return e ? e : (millis() / 1000);
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

    // Adjust the recorded boot epoch on the first accurate sync so that
    // bootEpoch() reflects the true wall-clock boot time.
    if (_bootRecorded && _bootEpoch < 1700000000) {
        uint32_t elapsed = (millis() - _bootMillis) / 1000;
        if (utcEpoch > elapsed) {
            _bootEpoch = utcEpoch - elapsed;
            LOGF("[Time] Boot epoch adjusted to %lu\n", (unsigned long)_bootEpoch);
        }
    }
}

void TimeHelper::recordBootTime() {
    if (_bootRecorded) return;
    _bootRecorded = true;
    _bootMillis = millis();
    _bootEpoch = bestEpoch();
    LOGF("[Time] Boot recorded: millis=%lu epoch=%lu\n",
         (unsigned long)_bootMillis, (unsigned long)_bootEpoch);
}

void TimeHelper::formatAgo(uint32_t diffSeconds, char* buf, size_t bufLen) {
    if (bufLen < 8) { buf[0] = '\0'; return; }
    if (diffSeconds < 60)       { snprintf(buf, bufLen, t("time_s"), (int)diffSeconds); }
    else if (diffSeconds < 3600){ snprintf(buf, bufLen, t("time_m"), (int)(diffSeconds / 60)); }
    else if (diffSeconds < 86400){ snprintf(buf, bufLen, t("time_h"), (int)(diffSeconds / 3600)); }
    else                        { snprintf(buf, bufLen, t("time_d"), (int)(diffSeconds / 86400)); }
}

void TimeHelper::formatTimestamp(uint32_t utcEpoch, char* buf, size_t bufLen) const {
    if (utcEpoch < 1700000000 || bufLen < 17) {
        buf[0] = '\0';
        return;
    }
    time_t t = (time_t)utcEpoch;
    struct tm result;
    localtime_r(&t, &result);
    snprintf(buf, bufLen, "%04d-%02d-%02d %02d:%02d",
             result.tm_year + 1900, result.tm_mon + 1, result.tm_mday,
             result.tm_hour, result.tm_min);
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
