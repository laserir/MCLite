#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Which zoom level the map should open on.
//
// Pulled out of MapScreen into a pure function with no LVGL or SD dependency so
// the rule can be unit-tested on the host: the interesting part is which tile
// packs land where, and every one of those cases is a table entry rather than
// something you can only find by loading a real pack onto a card.

namespace mclite {

// The "where am I" scale: town and road level, still showing enough ground to
// place yourself. Opening deeper than this drops you on a street corner with no
// context, which is the first thing everyone used to zoom out of.
constexpr uint8_t MAP_PREFERRED_ZOOM = 10;

// Index into `zooms` (assumed ascending, as TileLoader sorts it) of the level to
// open on. `hasTile[i]` says whether the centre tile exists at `zooms[i]`.
//
// Picks the available zoom CLOSEST to MAP_PREFERRED_ZOOM rather than the deepest
// one at or below it. Both directions matter:
//   - a city pack of 11-14 has nothing at or below 10, and opening at 14 would
//     reproduce exactly the problem this rule exists to avoid; 11 is right.
//   - a pack holding 5 and 12 would open at continent scale under a
//     "deepest at or below" rule; 12 is far closer to what was asked for.
// Ties go to the coarser level, since more ground is the safer error for an
// opening view and zooming in is one tap.
//
// Returns the last index when no zoom has a centre tile, matching the original
// behaviour of falling back to the deepest level available.
inline int pickZoomIdx(const std::vector<uint8_t>& zooms, const std::vector<bool>& hasTile) {
    if (zooms.empty()) return 0;

    int best = -1;
    int bestDist = 0;
    for (size_t i = 0; i < zooms.size(); i++) {
        if (i >= hasTile.size() || !hasTile[i]) continue;
        const int z = (int)zooms[i];
        const int dist = z > (int)MAP_PREFERRED_ZOOM ? z - (int)MAP_PREFERRED_ZOOM
                                                     : (int)MAP_PREFERRED_ZOOM - z;
        // Strictly-less keeps the FIRST (coarsest) of equally distant levels,
        // because `zooms` is ascending.
        if (best < 0 || dist < bestDist) { best = (int)i; bestDist = dist; }
    }
    return best >= 0 ? best : (int)zooms.size() - 1;
}

}  // namespace mclite
