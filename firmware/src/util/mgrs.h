#pragma once

#include <Arduino.h>
#include <math.h>

namespace mclite {

// WGS84 ellipsoid constants
static constexpr double MGRS_A  = 6378137.0;           // Semi-major axis
static constexpr double MGRS_F  = 1.0 / 298.257223563; // Flattening
static constexpr double MGRS_E2 = 2 * MGRS_F - MGRS_F * MGRS_F; // First eccentricity squared
static constexpr double MGRS_K0 = 0.9996;              // UTM scale factor

// Latitude band letters (C-X, excluding I and O)
static const char MGRS_BAND_LETTERS[] = "CDEFGHJKLMNPQRSTUVWX";

// 100km column letters cycle (per zone set 1-6)
static const char MGRS_COL_LETTERS_1[] = "ABCDEFGH";  // Sets 1,4
static const char MGRS_COL_LETTERS_2[] = "JKLMNPQR";  // Sets 2,5
static const char MGRS_COL_LETTERS_3[] = "STUVWXYZ";  // Sets 3,6

// 100km row letters cycle (alternating for odd/even zones)
static const char MGRS_ROW_LETTERS_ODD[]  = "ABCDEFGHJKLMNPQRSTUV"; // 20 letters
static const char MGRS_ROW_LETTERS_EVEN[] = "FGHJKLMNPQRSTUVABCDE"; // 20 letters

inline int utmZoneNumber(double lat, double lon) {
    int zone = (int)((lon + 180.0) / 6.0) + 1;
    // Norway exception
    if (lat >= 56.0 && lat < 64.0 && lon >= 3.0 && lon < 12.0) zone = 32;
    // Svalbard exceptions
    if (lat >= 72.0 && lat < 84.0) {
        if (lon >= 0.0  && lon <  9.0) zone = 31;
        else if (lon >= 9.0  && lon < 21.0) zone = 33;
        else if (lon >= 21.0 && lon < 33.0) zone = 35;
        else if (lon >= 33.0 && lon < 42.0) zone = 37;
    }
    return zone;
}

inline char utmBandLetter(double lat) {
    if (lat < -80.0 || lat > 84.0) return 'Z'; // Outside UTM
    int idx = (int)((lat + 80.0) / 8.0);
    if (idx > 19) idx = 19;
    return MGRS_BAND_LETTERS[idx];
}

// Lat/lon (WGS84) to UTM easting/northing
inline void latLonToUTM(double lat, double lon, int zone,
                        double& easting, double& northing) {
    double latRad = lat * M_PI / 180.0;
    double lonRad = lon * M_PI / 180.0;
    double lonOrigin = (zone - 1) * 6.0 - 180.0 + 3.0;
    double lonOriginRad = lonOrigin * M_PI / 180.0;

    double e2 = MGRS_E2;
    double ep2 = e2 / (1.0 - e2); // Second eccentricity squared

    double sinLat = sin(latRad);
    double cosLat = cos(latRad);
    double tanLat = tan(latRad);

    double N = MGRS_A / sqrt(1.0 - e2 * sinLat * sinLat);
    double T = tanLat * tanLat;
    double C = ep2 * cosLat * cosLat;
    double A = cosLat * (lonRad - lonOriginRad);

    // Meridional arc (M)
    double e4 = e2 * e2;
    double e6 = e4 * e2;
    double M = MGRS_A * ((1.0 - e2/4.0 - 3.0*e4/64.0 - 5.0*e6/256.0) * latRad
             - (3.0*e2/8.0 + 3.0*e4/32.0 + 45.0*e6/1024.0) * sin(2.0*latRad)
             + (15.0*e4/256.0 + 45.0*e6/1024.0) * sin(4.0*latRad)
             - (35.0*e6/3072.0) * sin(6.0*latRad));

    double A2 = A * A;
    double A3 = A2 * A;
    double A4 = A3 * A;
    double A5 = A4 * A;
    double A6 = A5 * A;

    easting = MGRS_K0 * N * (A + (1.0-T+C)*A3/6.0
              + (5.0-18.0*T+T*T+72.0*C-58.0*ep2)*A5/120.0) + 500000.0;

    northing = MGRS_K0 * (M + N * tanLat * (A2/2.0 + (5.0-T+9.0*C+4.0*C*C)*A4/24.0
               + (61.0-58.0*T+T*T+600.0*C-330.0*ep2)*A6/720.0));

    if (lat < 0.0) northing += 10000000.0; // Southern hemisphere offset
}

// Convert lat/lon to MGRS string
// precision: 1=10km, 2=1km, 3=100m, 4=10m, 5=1m
inline String latLonToMGRS(double lat, double lon, int precision = 4) {
    // Clamp to UTM bounds (skip polar UPS)
    if (lat < -80.0 || lat > 84.0) return "Outside UTM";

    int zone = utmZoneNumber(lat, lon);
    char band = utmBandLetter(lat);

    double easting, northing;
    latLonToUTM(lat, lon, zone, easting, northing);

    // 100km square identification
    int setNumber = ((zone - 1) % 6) + 1;

    // Column letter
    int col100k = (int)(easting / 100000.0);
    if (col100k < 1) col100k = 1;  // Defensive: easting always >= 100km in valid UTM
    const char* colLetters;
    switch (((setNumber - 1) % 3)) {
        case 0: colLetters = MGRS_COL_LETTERS_1; break;
        case 1: colLetters = MGRS_COL_LETTERS_2; break;
        default: colLetters = MGRS_COL_LETTERS_3; break;
    }
    char colLetter = colLetters[(col100k - 1) % 8];

    // Row letter
    int row100k = (int)(fmod(northing, 2000000.0) / 100000.0);
    const char* rowLetters = (setNumber % 2 != 0)
        ? MGRS_ROW_LETTERS_ODD : MGRS_ROW_LETTERS_EVEN;
    char rowLetter = rowLetters[row100k % 20];

    // Grid coordinates within 100km square
    int eastGrid  = (int)fmod(easting, 100000.0);
    int northGrid = (int)fmod(northing, 100000.0);

    // Truncate to requested precision
    int divisor = 1;
    for (int i = precision; i < 5; i++) divisor *= 10;
    eastGrid  /= divisor;
    northGrid /= divisor;

    // Format: "33U UP 9140 7180" (for precision=4)
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%c %c%c %0*d %0*d",
             zone, band, colLetter, rowLetter,
             precision, eastGrid, precision, northGrid);
    return String(buf);
}

} // namespace mclite
