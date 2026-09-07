#pragma once

#include "ConfigManager.h"
#include "../util/offgrid.h"

// Offgrid presets — the "meet me off the public mesh" radio settings.
//
// Why this is a table and not a constant (GitHub #49): the two official MeshCore
// clients disagree. The Android app puts offgrid on 869.945 MHz, MeshCore Open
// uses 869.000. There is no single correct value to hardcode, only a choice of
// which client to be compatible with, so the user picks.
//
// `sf == 0` means "change only the frequency and inherit the user's normal modem
// settings". That is what stock MeshCore does, and it is the default here so an
// upgrade changes nobody's radio. It is also subtly broken as a meeting point:
// two nodes whose normal regions differ (EU/UK Narrow SF8/CR8 vs Netherlands
// SF7/CR5) land on the same frequency and still cannot hear each other, because
// LoRa cannot demodulate across a different spreading factor or bandwidth.
// A preset that names its own SF/BW/CR fixes that, at the cost of deliberately
// diverging from stock's frequency-only switch.
//
// Adding or retiring an entry is meant to be cheap: this list, the config tool's
// OFFGRID_PRESETS, and a label key in strings.h.

namespace mclite {

struct OffgridPreset {
    const char* key;     // stable id; also what lands in config.json
    const char* labelKey;// i18n key for the picker label
    float       freq;    // MHz, or 0 = derive from the user's frequency (433/869/918)
    uint8_t     sf;      // 0 = inherit the user's SF/BW/CR (stock behaviour)
    float       bw;      // kHz; ignored when sf == 0
    uint8_t     cr;      // ignored when sf == 0
};

// Keep in sync with tools/config-tool OFFGRID_PRESETS.
static constexpr OffgridPreset OFFGRID_PRESETS[] = {
    // Default. Reproduces MCLite's original behaviour exactly: nearest of
    // 433/869/918 derived from the configured frequency, modem settings untouched.
    { "auto",       "og_auto",      0.0f,     0,  0.0f,  0 },
    // The two official clients, named so a group can match whichever they run.
    { "mc_open",    "og_mc_open",   869.000f, 0,  0.0f,  0 },
    { "mc_app",     "og_mc_app",    869.945f, 0,  0.0f,  0 },
    { "mc_433",     "og_mc_433",    433.000f, 0,  0.0f,  0 },
    { "mc_918",     "og_mc_918",    918.000f, 0,  0.0f,  0 },
    // Complete preset: everyone who picks it agrees on the modem settings too, so
    // it works between users whose normal regions differ. MCLite-to-MCLite only.
    { "mclite_869", "og_mclite_869", 869.000f, 8, 62.5f, 8 },
    // Values come from offgrid.frequency / spreading_factor / bandwidth / coding_rate.
    { "custom",     "og_custom",    0.0f,     0,  0.0f,  0 },
};
static constexpr size_t OFFGRID_PRESET_COUNT =
    sizeof(OFFGRID_PRESETS) / sizeof(OFFGRID_PRESETS[0]);

// Acceptable ranges. Deliberately the same bounds ConfigManager applies to the
// normal radio block, so the two can never disagree about what the hardware takes:
// out of range here means out of range there. A value that fails is not clamped to
// an edge -- silently moving someone to 150 MHz would be worse than ignoring the
// field -- it is dropped, and the user's normal setting is inherited instead.
inline bool offgridFreqValid(float f) { return f >= 150.0f && f <= 960.0f; }
inline bool offgridSfValid(uint8_t sf) { return sf >= 5 && sf <= 12; }
inline bool offgridBwValid(float bw)   { return bw >= 7.8f && bw <= 500.0f; }
inline bool offgridCrValid(uint8_t cr) { return cr >= 5 && cr <= 8; }

// Index of `key`, or 0 (auto) when unknown or empty. Unknown keys fall back
// rather than failing: a config written by a newer build that has since dropped
// an entry still boots, on the original behaviour.
inline size_t offgridPresetIndex(const String& key) {
    for (size_t i = 0; i < OFFGRID_PRESET_COUNT; i++) {
        if (key == OFFGRID_PRESETS[i].key) return i;
    }
    return 0;
}

// The radio settings offgrid mode actually applies. Pure, so it is unit-tested
// on the host: this is the one place that decides what goes on the air.
struct OffgridRadio {
    float   frequency;
    uint8_t spreadingFactor;
    float   bandwidth;
    uint8_t codingRate;
};

// Apply one set of four optional values onto `out`. 0 means "not set" (inherit),
// and anything set but out of range is ignored the same way -- this is the single
// gate every user-supplied number passes through before it can reach the radio,
// whether it came from `custom`, from an offgrid.presets[] entry, or from a
// companion/on-device edit.
inline void applyOffgridOverrides(OffgridRadio& out, float freq, uint8_t sf, float bw, uint8_t cr) {
    if (freq > 0.0f && offgridFreqValid(freq)) out.frequency       = freq;
    if (sf   > 0    && offgridSfValid(sf))     out.spreadingFactor = sf;
    if (bw   > 0.0f && offgridBwValid(bw))     out.bandwidth       = bw;
    if (cr   > 0    && offgridCrValid(cr))     out.codingRate      = cr;
}

inline OffgridRadio resolveOffgrid(const OffgridConfig& o, const RadioConfig& r) {
    // Start from the user's normal settings; a preset overrides what it names.
    OffgridRadio out{ offgridFreqFor(r.frequency), r.spreadingFactor, r.bandwidth, r.codingRate };

    if (o.preset == "custom") {
        // Each custom field is independent: set the ones you care about, leave
        // the rest to inherit.
        applyOffgridOverrides(out, o.frequency, o.spreadingFactor, o.bandwidth, o.codingRate);
        return out;
    }

    // A built-in key wins over a config-defined one of the same name, so a
    // config can never redefine "auto" out from under the fallback path.
    for (size_t i = 0; i < OFFGRID_PRESET_COUNT; i++) {
        if (o.preset == OFFGRID_PRESETS[i].key) {
            const OffgridPreset& p = OFFGRID_PRESETS[i];
            if (p.freq > 0.0f) out.frequency = p.freq;
            if (p.sf   > 0) {
                out.spreadingFactor = p.sf;
                out.bandwidth       = p.bw;
                out.codingRate      = p.cr;
            }
            return out;
        }
    }

    // Then a user-defined preset from config (offgrid.presets[]), matched by name.
    for (const auto& up : o.presets) {
        if (o.preset == up.name) {
            applyOffgridOverrides(out, up.frequency, up.spreadingFactor, up.bandwidth, up.codingRate);
            return out;
        }
    }

    // Unknown name: fall through on "auto" behaviour rather than failing.
    return out;
}

}  // namespace mclite
