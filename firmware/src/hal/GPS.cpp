#include "GPS.h"
#include "../util/mgrs.h"
#include "../util/epoch.h"
#include "../config/ConfigManager.h"
#include <Arduino.h>

namespace mclite {

GPS& GPS::instance() {
    static GPS inst;
    return inst;
}

bool GPS::init() {
    Serial1.begin(TDECK_GPS_BAUD, SERIAL_8N1, TDECK_GPS_RX, TDECK_GPS_TX);
    _enabled = true;
    Serial.println("[GPS] Initialized");
    return true;
}

void GPS::update() {
    if (!_enabled) return;
    while (Serial1.available()) {
        _gps.encode(Serial1.read());
    }
    // Track time sync based on current GPS data validity
    // hasTime() checks isValid() AND year >= 2024
    bool nowSynced = hasTime();
    if (nowSynced && !_timeSynced) {
        Serial.printf("[GPS] Time synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      _gps.date.year(), _gps.date.month(), _gps.date.day(),
                      hour(), minute(), second());
    } else if (!nowSynced && _timeSynced) {
        Serial.println("[GPS] Time lost");
    }
    _timeSynced = nowSynced;

    // Cache position when we have a fresh, quality fix
    if (_gps.location.isValid() && _gps.location.age() < 2000) {
        float currentHdop = hdop();
        if (currentHdop < 25.0f) {
            _cached.lat = _gps.location.lat();
            _cached.lon = _gps.location.lng();
            _cached.altitude = _gps.altitude.meters();
            _cached.fixMillis = millis();
            _cached.satellites = _gps.satellites.value();
            _cached.hdop = currentHdop;
            _cached.valid = true;
        }
    }
}

FixStatus GPS::fixStatus() const {
    if (hasFix()) return FixStatus::LIVE;
    if (_cached.valid && fixAgeSeconds() <= _lastKnownMaxAge) return FixStatus::LAST_KNOWN;
    return FixStatus::NO_FIX;
}

uint32_t GPS::fixAgeSeconds() const {
    if (!_cached.valid) return UINT32_MAX;
    return (millis() - _cached.fixMillis) / 1000;
}

uint32_t GPS::currentTimestamp() const {
    if (!hasTime()) return 0;
    return dateToEpoch(_gps.date.year(), _gps.date.month(), _gps.date.day(),
                       hour(), minute(), second());
}

String GPS::formatLocation() const {
    // Determine which position to use
    double posLat, posLon;
    FixStatus status = fixStatus();

    if (status == FixStatus::LIVE) {
        posLat = lat();
        posLon = lon();
    } else if (status == FixStatus::LAST_KNOWN) {
        posLat = _cached.lat;
        posLon = _cached.lon;
    } else {
        return "No GPS fix";
    }

    const auto& cfg = ConfigManager::instance().config();
    const String& fmt = cfg.messaging.locationFormat;

    char latlonBuf[48];
    snprintf(latlonBuf, sizeof(latlonBuf), "%.6f, %.6f", posLat, posLon);

    if (fmt == "mgrs") {
        return latLonToMGRS(posLat, posLon, 4);
    } else if (fmt == "both") {
        return String(latlonBuf) + " (" + latLonToMGRS(posLat, posLon, 4) + ")";
    }
    // Default: "decimal" / "latlon"
    return String(latlonBuf);
}

String GPS::formatLocationWithStatus() const {
    FixStatus status = fixStatus();
    if (status == FixStatus::NO_FIX) return "No GPS fix";

    String loc = formatLocation();

    if (status == FixStatus::LAST_KNOWN) {
        uint32_t age = fixAgeSeconds();
        char ageBuf[16];
        if (age < 60) {
            snprintf(ageBuf, sizeof(ageBuf), "~%ds ago", (int)age);
        } else if (age < 3600) {
            snprintf(ageBuf, sizeof(ageBuf), "~%dm ago", (int)(age / 60));
        } else {
            snprintf(ageBuf, sizeof(ageBuf), "~%dh ago", (int)(age / 3600));
        }
        loc += " [";
        loc += ageBuf;
        loc += "]";
    }

    return loc;
}

}  // namespace mclite
