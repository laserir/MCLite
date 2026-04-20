#include "TileLoader.h"
#include "SDCard.h"
#include <PNGdec.h>
#include <SD.h>
#include <Arduino.h>
#include <algorithm>
#include <new>

namespace mclite {

namespace {

// Context passed to the PNGdec draw callback: destination canvas + where this
// tile should land within it.
struct DrawCtx {
    lv_color_t* buf;
    int bufW;
    int bufH;
    int dstX;   // top-left x in canvas for this tile
    int dstY;   // top-left y in canvas for this tile
};

// PNGdec file callbacks: wrap an Arduino SD `File` via PNGFILE::fHandle.
void* pngOpenCb(const char* filename, int32_t* size) {
    File* f = new File(SD.open(filename, FILE_READ));
    if (!f || !*f) {
        delete f;
        return nullptr;
    }
    *size = f->size();
    return f;
}

void pngCloseCb(void* handle) {
    File* f = static_cast<File*>(handle);
    if (f) {
        f->close();
        delete f;
    }
}

int32_t pngReadCb(PNGFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    File* f = static_cast<File*>(pFile->fHandle);
    if (!f) return 0;
    return f->read(pBuf, iLen);
}

int32_t pngSeekCb(PNGFILE* pFile, int32_t iPosition) {
    File* f = static_cast<File*>(pFile->fHandle);
    if (!f) return 0;
    return f->seek(iPosition) ? iPosition : 0;
}

}  // namespace

// File-scope pointer used by the PNGdec draw callback to call back into
// getLineAsRGB565(). PNGdec's draw-callback signature has no room for a
// PNG* parameter, so we stash the current instance here during decode().
static PNG* _pngCurrent = nullptr;

namespace {

// Scanline callback: convert to RGB565, blit into the canvas buffer with
// per-pixel clipping. LVGL is configured with LV_COLOR_16_SWAP=1, so we ask
// PNGdec for big-endian RGB565 to match.
int pngDrawCb(PNGDRAW* pDraw) {
    DrawCtx* ctx = static_cast<DrawCtx*>(pDraw->pUser);
    if (!ctx || !ctx->buf || !_pngCurrent) return 0;

    static uint16_t lineBuf[256];  // one scanline of a 256-wide tile
    _pngCurrent->getLineAsRGB565(pDraw, lineBuf, PNG_RGB565_BIG_ENDIAN, 0);

    const int y = ctx->dstY + pDraw->y;
    if (y < 0 || y >= ctx->bufH) return 1;  // row off-canvas; keep decoding

    const int w = pDraw->iWidth;
    lv_color_t* dstRow = ctx->buf + y * ctx->bufW;
    for (int i = 0; i < w; i++) {
        const int x = ctx->dstX + i;
        if (x < 0 || x >= ctx->bufW) continue;
        // lineBuf holds RGB565 big-endian; lv_color_t at 16-bit with
        // LV_COLOR_16_SWAP=1 stores the same byte-swapped layout.
        lv_color_t c;
        c.full = lineBuf[i];
        dstRow[x] = c;
    }
    return 1;
}

// Fill a 256x256 area (clipped to canvas) with mid-grey as a placeholder for
// a missing tile.
void fillGrey(lv_color_t* buf, int bufW, int bufH, int dstX, int dstY) {
    const int tile = 256;
    const lv_color_t grey = lv_color_make(0x60, 0x60, 0x60);
    const int x0 = std::max(0, dstX);
    const int y0 = std::max(0, dstY);
    const int x1 = std::min(bufW, dstX + tile);
    const int y1 = std::min(bufH, dstY + tile);
    for (int y = y0; y < y1; y++) {
        lv_color_t* row = buf + y * bufW;
        for (int x = x0; x < x1; x++) row[x] = grey;
    }
}

}  // namespace

TileLoader& TileLoader::instance() {
    static TileLoader inst;
    return inst;
}

void TileLoader::init() {
    if (_initialised) return;
    scan();
    _initialised = true;
}

void TileLoader::scan() {
    _zooms.clear();
    _present = false;

    auto& sd = SDCard::instance();
    if (!sd.isMounted()) return;
    if (!sd.dirExists("/tiles")) return;

    File root = SD.open("/tiles");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            String name = entry.name();
            // Arduino SD may return full path or bare name; take last segment.
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (!name.startsWith("._") && name.length() > 0 && name.length() <= 2) {
                bool numeric = true;
                for (size_t i = 0; i < name.length(); i++) {
                    if (!isdigit((unsigned char)name[i])) { numeric = false; break; }
                }
                if (numeric) {
                    int z = name.toInt();
                    if (z >= 0 && z <= 19) _zooms.push_back((uint8_t)z);
                }
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    std::sort(_zooms.begin(), _zooms.end());
    _zooms.erase(std::unique(_zooms.begin(), _zooms.end()), _zooms.end());
    _present = !_zooms.empty();

    Serial.printf("[TileLoader] /tiles: %d zoom levels (%u..%u)\n",
                  (int)_zooms.size(),
                  _zooms.empty() ? 0u : (unsigned)_zooms.front(),
                  _zooms.empty() ? 0u : (unsigned)_zooms.back());
}

bool TileLoader::tilesAvailable() {
    if (!_initialised) init();
    return _present;
}

const std::vector<uint8_t>& TileLoader::availableZooms() {
    if (!_initialised) init();
    return _zooms;
}

bool TileLoader::tileExists(uint8_t z, int tx, int ty) {
    if (!tilesAvailable()) return false;
    char path[48];
    snprintf(path, sizeof(path), "/tiles/%u/%d/%d.png", (unsigned)z, tx, ty);
    return SDCard::instance().fileExists(path);
}

bool TileLoader::ensurePngWorkspace() {
    if (_pngStorage) return true;
    _pngStorage = ps_malloc(sizeof(PNG));
    if (!_pngStorage) _pngStorage = malloc(sizeof(PNG));  // PSRAM exhausted: try DRAM
    return _pngStorage != nullptr;
}

bool TileLoader::decodeInto(lv_color_t* buf, int bufW, int bufH,
                            int dstX, int dstY, uint8_t z, int tx, int ty) {
    if (!buf) return false;

    char path[48];
    snprintf(path, sizeof(path), "/tiles/%u/%d/%d.png", (unsigned)z, tx, ty);

    if (!SDCard::instance().fileExists(path)) {
        fillGrey(buf, bufW, bufH, dstX, dstY);
        return false;
    }

    if (!ensurePngWorkspace()) {
        fillGrey(buf, bufW, bufH, dstX, dstY);
        return false;
    }

    PNG* png = new (_pngStorage) PNG();
    int rc = png->open(path, pngOpenCb, pngCloseCb, pngReadCb, pngSeekCb, pngDrawCb);
    if (rc != PNG_SUCCESS) {
        Serial.printf("[TileLoader] open failed: %s (rc=%d)\n", path, rc);
        png->~PNG();
        fillGrey(buf, bufW, bufH, dstX, dstY);
        return false;
    }

    DrawCtx ctx{buf, bufW, bufH, dstX, dstY};
    _pngCurrent = png;
    rc = png->decode(&ctx, 0);
    _pngCurrent = nullptr;
    png->close();
    png->~PNG();

    if (rc != PNG_SUCCESS) {
        Serial.printf("[TileLoader] decode failed: %s (rc=%d)\n", path, rc);
        fillGrey(buf, bufW, bufH, dstX, dstY);
        return false;
    }
    return true;
}

}  // namespace mclite
