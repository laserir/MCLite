#pragma once

#include <TinyGPSPlus.h>

namespace mclite {

enum class FixStatus { NO_FIX, LAST_KNOWN, LIVE };

struct CachedPosition {
    double lat = 0;
    double lon = 0;
    double altitude = 0;
    uint32_t fixMillis = 0;   // millis() when this fix was captured
    uint8_t satellites = 0;
    float hdop = 99.9f;
    bool valid = false;        // has ever had a fix
};

class GPS {
public:
    bool init();
    void update();  // Call from main loop

    bool     hasFix() const   { return _gps.location.isValid() && _gps.location.age() < 2000; }
    double   lat() const      { return _gps.location.lat(); }
    double   lon() const      { return _gps.location.lng(); }
    double   altitude() const { return _gps.altitude.meters(); }
    uint8_t  satellites() const { return _gps.satellites.value(); }
    float    hdop() const     { return _gps.hdop.isValid() ? (float)_gps.hdop.hdop() : 99.9f; }

    // Fix status with last-known position support
    FixStatus fixStatus() const;
    const CachedPosition& lastPosition() const { return _cached; }
    double   cachedLat() const  { return _cached.lat; }
    double   cachedLon() const  { return _cached.lon; }
    uint32_t fixAgeSeconds() const;

    // Max age for last-known position (configurable, default 30 min)
    void     setLastKnownMaxAge(uint32_t seconds) { _lastKnownMaxAge = seconds; }

    // Time from GPS (UTC) — requires valid NMEA data AND sane date (year >= 2024)
    bool     hasTime() const  { return _gps.time.isValid() && _gps.date.isValid()
                                       && _gps.date.year() >= 2024; }
    uint8_t  hour() const     { return _gps.time.hour(); }
    uint8_t  minute() const   { return _gps.time.minute(); }
    uint8_t  second() const   { return _gps.time.second(); }

    // Time sync state: true when GPS currently has valid time data.
    // Unlike hasFix() (which needs position), this only needs valid date+time.
    bool     isTimeSynced() const { return _timeSynced; }

    // Unix epoch timestamp (seconds since 1970-01-01), matching MeshCore convention.
    // Returns 0 if GPS time not available.
    uint32_t currentTimestamp() const;

    // Format location as string (respects coordFormat config)
    String   formatLocation() const;

    // Format with explicit fix status and age qualifier
    String   formatLocationWithStatus() const;

    TinyGPSPlus& raw() { return _gps; }

    static GPS& instance();

private:
    GPS() = default;
    mutable TinyGPSPlus _gps;
    bool _enabled    = false;
    bool _timeSynced = false;
    CachedPosition _cached;
    uint32_t _lastKnownMaxAge = 1800; // 30 minutes default
};

}  // namespace mclite
