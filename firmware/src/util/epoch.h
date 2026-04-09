#pragma once

#include <cstdint>

namespace mclite {

// Convert date + time components to Unix epoch seconds (since 1970-01-01 00:00 UTC).
// Returns 0 if year < 2024 or month/day are 0 (invalid GPS data).
inline uint32_t dateToEpoch(uint16_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t minute, uint8_t second) {
    if (year < 2024 || month == 0 || day == 0) return 0;

    auto isLeap = [](uint16_t yr) {
        return yr % 4 == 0 && (yr % 100 != 0 || yr % 400 == 0);
    };

    uint32_t days = 0;
    for (uint16_t yr = 1970; yr < year; yr++) {
        days += isLeap(yr) ? 366 : 365;
    }

    static const uint8_t daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (uint8_t mo = 1; mo < month; mo++) {
        days += daysInMonth[mo - 1];
        if (mo == 2 && isLeap(year)) days++;
    }
    days += (day - 1);

    return days * 86400UL + (uint32_t)hour * 3600 + (uint32_t)minute * 60 + second;
}

}  // namespace mclite
