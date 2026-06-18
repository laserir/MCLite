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

    // Call every loop. No-op unless WiFi is connected AND the clock isn't already
    // synced (GPS wins). Starts SNTP once per connection and polls non-blocking;
    // sets the clock once a time arrives, honoring the configured timezone.
    void maybeNtpSync();

    // The effective POSIX TZ string applyTimezone() last applied.
    const String& posixTz() const { return _tz; }

    // Format UTC epoch as "HH:MM" in local time. Writes "" if invalid.
    void formatHHMM(uint32_t utcEpoch, char* buf, size_t bufLen) const;

    // Minimal POSIX TZ validation: needs alpha prefix + at least one digit.
    static bool isValidPosixTz(const String& tz);

    bool isSynced() const { return _synced; }

    // Current Unix epoch from the system clock if a sync has happened
    // (RTC-restore at boot or GPS lock during runtime); 0 otherwise.
    uint32_t nowEpoch() const;

    // nowEpoch() if synced, millis()/1000 otherwise. Use for outgoing
    // message timestamps where we always need *some* value (receivers
    // will see 1970-01-XX in the fallback case but local sort works).
    uint32_t bestEpoch() const;

    // Record the boot instant using the best available clock.
    // Call once from setup() after RTC restore / timezone apply.
    void recordBootTime();

    // Wall-clock epoch when the device booted. 0 if not yet recorded.
    uint32_t bootEpoch() const { return _bootEpoch; }

    // Format a duration (seconds) as a relative-time string using the
    // existing i18n keys (time_s, time_m, time_h, time_d).
    static void formatAgo(uint32_t diffSeconds, char* buf, size_t bufLen);

    // Format UTC epoch as "%Y-%m-%d %H:%M" in local time. Writes "" if invalid.
    void formatTimestamp(uint32_t utcEpoch, char* buf, size_t bufLen) const;

private:
    TimeHelper() = default;
    bool     _synced = false;
    uint32_t _lastSyncEpoch = 0;
    String   _tz;                 // effective POSIX TZ from applyTimezone()
    bool     _ntpStarted = false; // SNTP kicked off for the current WiFi connection

    // Boot-time tracking
    bool     _bootRecorded = false;
    uint32_t _bootMillis = 0;     // millis() at recordBootTime()
    uint32_t _bootEpoch = 0;      // bestEpoch() at recordBootTime(); adjusted on first sync
};

}  // namespace mclite
