#pragma once

// Offgrid (client-repeat) community frequency whitelist.
// Deterministic: the device always picks the closest of 433 / 869 / 918 MHz
// from the user's normal frequency, guaranteeing interoperability with the
// official MeshCore offgrid network without any extra configuration.

namespace mclite {

inline float offgridFreqFor(float normalFreq) {
    if (normalFreq < 600.0f)  return 433.0f;
    if (normalFreq < 894.0f)  return 869.0f;
    return 918.0f;
}

}  // namespace mclite
