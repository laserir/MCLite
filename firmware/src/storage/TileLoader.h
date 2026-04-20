#pragma once

#include <stdint.h>
#include <vector>
#include <lvgl.h>

namespace mclite {

// Slippy-tile loader: PNG decoder that streams /tiles/{z}/{x}/{y}.png from
// the SD card directly into an LVGL canvas buffer. Standard OSM layout,
// 256x256 tiles, Web Mercator. Compatible with MeshCore's tile convention.
class TileLoader {
public:
    static TileLoader& instance();

    // Lazy init: call once after SD mount. Safe to re-call.
    void init();

    // Whether /tiles exists on SD and has at least one numeric zoom subfolder.
    bool tilesAvailable();

    // Sorted ascending list of zoom levels found under /tiles.
    const std::vector<uint8_t>& availableZooms();

    // True if the specific tile file exists on SD.
    bool tileExists(uint8_t z, int tx, int ty);

    // Decode the tile into the RGB565 canvas buffer, with the tile's top-left
    // landing at (dstX, dstY) in canvas pixels. Out-of-canvas pixels are
    // clipped. Missing tile -> grey-fills the tile area within the canvas.
    // Returns true if a tile was decoded; false if the tile was missing (grey).
    bool decodeInto(lv_color_t* buf, int bufW, int bufH,
                    int dstX, int dstY, uint8_t z, int tx, int ty);

private:
    TileLoader() = default;
    void scan();
    bool ensurePngWorkspace();

    bool _initialised = false;
    bool _present     = false;
    std::vector<uint8_t> _zooms;
    void* _pngStorage = nullptr;  // PSRAM-allocated PNG instance, reused
};

}  // namespace mclite
