#pragma once

#include "ConfigManager.h"
#include <math.h>

// Region radio presets — mirror the config tool's PRESETS table
// (tools/config-tool: eu_narrow / us_ca). On-device the Radio screen offers
// these as a picker instead of raw freq/SF/BW/CR fields, so users can't make a
// regulatory/compat mistake. Applying a preset is a reboot-on-leave change.

namespace mclite {

struct RadioPreset {
    const char* key;     // stable id (matches config tool)
    const char* label;   // human label
    float       freq;    // MHz
    uint8_t     sf;
    float       bw;      // kHz
    uint8_t     cr;
    int8_t      tx;      // dBm
};

// Keep in sync with tools/config-tool PRESETS.
// Derived from https://api.meshcore.nz/api/v1/config
static constexpr RadioPreset RADIO_PRESETS[] = {
    { "eu_narrow",     "EU/UK/CH",          869.618f,  8,  62.5f, 8, 22 },
    { "us_ca",         "US/Canada",          910.525f,  7,  62.5f, 5, 22 },
    { "au_wide",       "Australia",          915.800f, 10, 250.0f, 5, 22 },
    { "au_narrow",     "AU Narrow",          916.575f,  7,  62.5f, 8, 22 },
    { "au_mid",        "AU Mid",             915.075f,  9, 125.0f, 5, 22 },
    { "au_sa_wa",      "AU SA/WA",           923.125f,  8,  62.5f, 8, 22 },
    { "au_qld",        "AU QLD",             923.125f,  8,  62.5f, 5, 22 },
    { "au_vic",        "AU Victoria",        916.575f, 10, 250.0f, 5, 22 },
    { "br",            "Brazil",             923.125f,  8,  62.5f, 8, 22 },
    { "cz_narrow",     "Czech Republic",     869.432f,  7,  62.5f, 5, 22 },
    { "eu_433_lr",     "EU 433 Long Range",  433.650f, 11, 250.0f, 5, 22 },
    { "eu_433_narrow", "EU 433 Narrow",      433.650f,  8,  62.5f, 8, 22 },
    { "nl",            "Netherlands",        869.618f,  7,  62.5f, 5, 22 },
    { "nz",            "New Zealand",        917.375f, 11, 250.0f, 5, 22 },
    { "nz_narrow",     "NZ Narrow",          917.375f,  7,  62.5f, 5, 22 },
    { "pt_433",        "Portugal 433",       433.375f,  9,  62.5f, 6, 22 },
    { "pt_868",        "Portugal 868",       869.618f,  7,  62.5f, 6, 22 },
    { "ch",            "Switzerland",        869.618f,  8,  62.5f, 8, 22 },
    { "vn_narrow",     "Vietnam (Narrow)",   920.250f,  8,  62.5f, 5, 22 },
};
static constexpr size_t RADIO_PRESET_COUNT = sizeof(RADIO_PRESETS) / sizeof(RADIO_PRESETS[0]);

// Apply a preset's frequency/SF/BW/CR/TX onto a RadioConfig (scope/path-hash
// untouched). Returns false if the index is out of range.
inline bool applyRadioPreset(RadioConfig& r, size_t idx) {
    if (idx >= RADIO_PRESET_COUNT) return false;
    const RadioPreset& p = RADIO_PRESETS[idx];
    r.frequency       = p.freq;
    r.spreadingFactor = p.sf;
    r.bandwidth       = p.bw;
    r.codingRate      = p.cr;
    r.txPower         = p.tx;
    return true;
}

// Index of the preset matching the current radio params, or -1 for "Custom".
// TX power is intentionally excluded — it's separately adjustable on-device, so
// a user-lowered TX still reads as its region preset.
inline int matchRadioPreset(const RadioConfig& r) {
    for (size_t i = 0; i < RADIO_PRESET_COUNT; i++) {
        const RadioPreset& p = RADIO_PRESETS[i];
        if (fabsf(r.frequency - p.freq) < 0.001f &&
            r.spreadingFactor == p.sf &&
            fabsf(r.bandwidth - p.bw) < 0.01f &&
            r.codingRate == p.cr) {
            return (int)i;
        }
    }
    return -1;
}

}  // namespace mclite
