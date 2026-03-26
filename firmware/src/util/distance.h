#pragma once

#include <Arduino.h>
#include <math.h>

namespace mclite {

static constexpr double EARTH_RADIUS_M = 6371000.0;

inline double haversineMeters(double lat1, double lon1, double lat2, double lon2) {
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double rLat1 = lat1 * M_PI / 180.0;
    double rLat2 = lat2 * M_PI / 180.0;

    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(rLat1) * cos(rLat2) * sin(dLon / 2) * sin(dLon / 2);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return EARTH_RADIUS_M * c;
}

inline String formatDistance(double meters) {
    if (meters >= 1000.0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f km", meters / 1000.0);
        return String(buf);
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d m", (int)meters);
        return String(buf);
    }
}

}  // namespace mclite
