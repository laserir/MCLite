#pragma once

#include <math.h>
#include <stdint.h>

namespace mclite {

// Web Mercator (EPSG:3857) slippy tile helpers.
// Layout matches OSM / MeshCore convention: /tiles/{z}/{x}/{y}.png, 256x256.

static constexpr int SLIPPY_TILE_SIZE = 256;

// Web Mercator clamps latitude to +/- this to avoid tan(pi/2) blowup.
static constexpr double SLIPPY_LAT_MAX = 85.05112878;

struct TileFrac {
    double x;  // fractional tile column
    double y;  // fractional tile row
};

inline int slippyTileCount(uint8_t z) {
    return 1 << z;
}

inline TileFrac latLonToTileXY(double lat, double lon, uint8_t z) {
    if (lat >  SLIPPY_LAT_MAX) lat =  SLIPPY_LAT_MAX;
    if (lat < -SLIPPY_LAT_MAX) lat = -SLIPPY_LAT_MAX;
    // Wrap lon into [-180, 180)
    while (lon >= 180.0) lon -= 360.0;
    while (lon < -180.0) lon += 360.0;

    const double n = (double)slippyTileCount(z);
    const double latRad = lat * M_PI / 180.0;
    TileFrac out;
    out.x = (lon + 180.0) / 360.0 * n;
    out.y = (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / M_PI) / 2.0 * n;
    return out;
}

// Ground distance per pixel at the given latitude and zoom (meters/pixel).
// Web Mercator standard: 156543.03 * cos(lat) / 2^z.
inline double metersPerPixel(double lat, uint8_t z) {
    const double latRad = lat * M_PI / 180.0;
    return 156543.03392804097 * cos(latRad) / (double)slippyTileCount(z);
}

}  // namespace mclite
