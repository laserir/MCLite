#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <time.h>
#include <Arduino.h>

// KEEP THIS HEADER LIGHT. test_timehelper includes it directly to exercise
// formatClock() below; the native_test env compiles no src/ files
// (platformio.ini build_src_filter = -<*>), so pulling ConfigManager, I18n or
// WiFiManager in here would force the test back to hand-copying the function.

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

    // Buffer sizes for the two formatters below, at their widest (12-hour).
    // Use these rather than hand-picked numbers: a buffer one byte short makes
    // the formatter write "" instead of truncating, which shows up as a blank
    // clock rather than an obvious error.
    static constexpr size_t CLOCK_BUF     = 10;  // "12:45 AM" = 8 + NUL, rounded up
    static constexpr size_t TIMESTAMP_BUF = 24;  // "2026-08-31 12:45 AM" = 19 + NUL

    // Write "23:45" (24-hour) or "1:45 PM" (12-hour). Returns false and writes ""
    // when the buffer is too small or the inputs are out of range, so a caller can
    // never silently render a truncated time.
    //
    // The hour is NOT zero-padded in 12-hour mode ("1:45 PM", not "01:45 PM") —
    // that is the English convention, and it saves a character in the T-Deck
    // status bar where the clock shares one row with every indicator.
    //
    // AM/PM are literal ASCII, deliberately not t(): routing them through I18n
    // would pull I18n.h into this header and break the native test, and the
    // shipped de/fr/it locales are 24-hour ones that would never enable this.
    static bool formatClock(char* buf, size_t bufLen, int hour24, int minute, bool h12) {
        if (!buf || bufLen == 0) return false;
        if (bufLen < (h12 ? 9u : 6u) ||
            hour24 < 0 || hour24 > 23 || minute < 0 || minute > 59) {
            buf[0] = '\0';
            return false;
        }
        if (!h12) {
            snprintf(buf, bufLen, "%02d:%02d", hour24, minute);
            return true;
        }
        const char* suffix = (hour24 < 12) ? "AM" : "PM";
        int h = hour24 % 12;
        if (h == 0) h = 12;               // 00:xx -> 12 AM, 12:xx -> 12 PM
        snprintf(buf, bufLen, "%d:%02d %s", h, minute, suffix);
        return true;
    }

    // Format UTC epoch as "HH:MM" (or "H:MM AM/PM") in local time, honouring
    // display.clock_12h. Writes "" if invalid. Needs CLOCK_BUF bytes.
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

    // Format UTC epoch as "%Y-%m-%d %H:%M" in local time, with the clock part
    // honouring display.clock_12h. Writes "" if invalid. Needs TIMESTAMP_BUF bytes.
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
