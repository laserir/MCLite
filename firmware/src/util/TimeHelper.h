#pragma once
#include <cstdint>
#include <time.h>
#include <Arduino.h>

// Leverages ESP32 newlib's POSIX timezone support for automatic DST handling.
// Configure once with a POSIX TZ string, localtime_r() does the rest.

namespace mclite {

class TimeHelper {
public:
    static TimeHelper& instance() {
        static TimeHelper inst;
        return inst;
    }

    // Call after config load — sets TZ env var and calls tzset().
    // If gpsTimezone is set, uses it directly as POSIX TZ string.
    // Falls back to gpsClockOffset (converted to fixed-offset TZ).
    void applyTimezone();

    // Call from loop() when GPS has valid time — sets ESP32 system clock.
    // Idempotent: skips if epoch hasn't changed.
    void syncSystemClock(uint32_t utcEpoch);

    // Format UTC epoch as "HH:MM" in local time. Writes "" if invalid.
    void formatHHMM(uint32_t utcEpoch, char* buf, size_t bufLen) const;

    // Minimal POSIX TZ validation: needs alpha prefix + at least one digit.
    static bool isValidPosixTz(const String& tz);

    bool isSynced() const { return _synced; }

private:
    TimeHelper() = default;
    bool     _synced = false;
    uint32_t _lastSyncEpoch = 0;
};

}  // namespace mclite
