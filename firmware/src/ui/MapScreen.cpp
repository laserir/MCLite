#include "MapScreen.h"
#include "UIManager.h"
#include "theme.h"
#include "../storage/TileLoader.h"
#include "../util/slippy.h"
#include "../hal/GPS.h"
#include "../input/Keyboard.h"
#include "../input/Trackball.h"
#include <math.h>
#include <algorithm>

namespace mclite {

static constexpr int TILE = SLIPPY_TILE_SIZE;  // 256

void MapScreen::open(double contactLat, double contactLon, const String& contactName) {
    if (_screen) return;  // already open

    _lat = contactLat;
    _lon = contactLon;
    _contactName = contactName;

    // Snapshot available zooms and pick an initial level before building the
    // canvas — if tiles are unavailable we bail out without allocating.
    _zooms = TileLoader::instance().availableZooms();
    if (_zooms.empty()) {
        Serial.println("[MapScreen] no tiles available; aborting open");
        return;
    }
    pickInitialZoom();

    // Allocate canvas buffer in PSRAM.
    _cbuf = (lv_color_t*)ps_malloc(CANVAS_W * CANVAS_H * sizeof(lv_color_t));
    if (!_cbuf) {
        Serial.println("[MapScreen] ps_malloc canvas failed");
        return;
    }

    _prevScreen = lv_scr_act();
    buildWidgets();

    // Swap input group: save previous (UIManager's main group), install our own.
    _prevGroup = UIManager::instance().inputGroup();
    _mapGroup = lv_group_create();
    lv_group_add_obj(_mapGroup, _closeBtn);
    lv_group_add_obj(_mapGroup, _zoomInBtn);
    lv_group_add_obj(_mapGroup, _zoomOutBtn);
    lv_group_focus_obj(_closeBtn);
    if (Keyboard::instance().indev())
        lv_indev_set_group(Keyboard::instance().indev(), _mapGroup);
    if (Trackball::instance().indev())
        lv_indev_set_group(Trackball::instance().indev(), _mapGroup);

    lv_scr_load(_screen);
    render();
    Serial.printf("[MapScreen] opened (%.5f, %.5f) z=%u\n", _lat, _lon, (unsigned)_zoom);
}

void MapScreen::close() {
    if (!_screen) return;

    // Restore input groups.
    if (_prevGroup) {
        if (Keyboard::instance().indev())
            lv_indev_set_group(Keyboard::instance().indev(), _prevGroup);
        if (Trackball::instance().indev())
            lv_indev_set_group(Trackball::instance().indev(), _prevGroup);
    }
    if (_mapGroup) {
        lv_group_del(_mapGroup);
        _mapGroup = nullptr;
    }

    // Restore previous screen and delete ours.
    if (_prevScreen && _prevScreen != _screen) {
        lv_scr_load(_prevScreen);
    }
    destroyWidgets();

    if (_cbuf) {
        free(_cbuf);
        _cbuf = nullptr;
    }
    _prevScreen = nullptr;
    _prevGroup = nullptr;
    _zooms.clear();
    Serial.println("[MapScreen] closed");
}

void MapScreen::pickInitialZoom() {
    // Pick the largest available zoom where the centre tile exists; fall back
    // to the largest available zoom if no centre tile is present.
    auto& loader = TileLoader::instance();
    int chosen = (int)_zooms.size() - 1;
    for (int i = (int)_zooms.size() - 1; i >= 0; i--) {
        uint8_t z = _zooms[i];
        TileFrac f = latLonToTileXY(_lat, _lon, z);
        int tx = (int)floor(f.x);
        int ty = (int)floor(f.y);
        if (loader.tileExists(z, tx, ty)) { chosen = i; break; }
    }
    _zoomIdx = chosen;
    _zoom = _zooms[_zoomIdx];
}

void MapScreen::buildWidgets() {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, lv_color_black(), 0);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(_screen, 0, 0);

    // Canvas (full-screen).
    _canvas = lv_canvas_create(_screen);
    lv_canvas_set_buffer(_canvas, _cbuf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(_canvas, 0, 0);

    auto styleBtn = [](lv_obj_t* b) {
        lv_obj_set_size(b, 28, 28);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_bg_color(b, theme::BG_SECONDARY, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, theme::TEXT_SECONDARY, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
    };

    // Close button (top-left).
    _closeBtn = lv_btn_create(_screen);
    styleBtn(_closeBtn);
    lv_obj_align(_closeBtn, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_t* closeLbl = lv_label_create(_closeBtn);
    lv_label_set_text(closeLbl, LV_SYMBOL_CLOSE);
    lv_obj_center(closeLbl);
    lv_obj_add_event_cb(_closeBtn, &MapScreen::closeBtnCb, LV_EVENT_CLICKED, this);

    // Zoom-in button (top-right).
    _zoomInBtn = lv_btn_create(_screen);
    styleBtn(_zoomInBtn);
    lv_obj_align(_zoomInBtn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_t* inLbl = lv_label_create(_zoomInBtn);
    lv_label_set_text(inLbl, LV_SYMBOL_PLUS);
    lv_obj_center(inLbl);
    lv_obj_add_event_cb(_zoomInBtn, &MapScreen::zoomInCb, LV_EVENT_CLICKED, this);

    // Zoom-out button (below +).
    _zoomOutBtn = lv_btn_create(_screen);
    styleBtn(_zoomOutBtn);
    lv_obj_align(_zoomOutBtn, LV_ALIGN_TOP_RIGHT, -4, 36);
    lv_obj_t* outLbl = lv_label_create(_zoomOutBtn);
    lv_label_set_text(outLbl, LV_SYMBOL_MINUS);
    lv_obj_center(outLbl);
    lv_obj_add_event_cb(_zoomOutBtn, &MapScreen::zoomOutCb, LV_EVENT_CLICKED, this);

    // Info label (bottom-left): zoom level + scale text.
    _infoLabel = lv_label_create(_screen);
    lv_obj_set_style_text_font(_infoLabel, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_infoLabel, theme::TEXT_PRIMARY, 0);
    lv_obj_set_style_bg_color(_infoLabel, theme::BG_SECONDARY, 0);
    lv_obj_set_style_bg_opa(_infoLabel, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(_infoLabel, 3, 0);
    lv_obj_align(_infoLabel, LV_ALIGN_BOTTOM_LEFT, 4, -4);

    // Screen-level key handler for Esc / +/- shortcuts.
    lv_obj_add_event_cb(_screen, &MapScreen::screenKeyCb, LV_EVENT_KEY, this);
}

void MapScreen::destroyWidgets() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _canvas = _closeBtn = _zoomInBtn = _zoomOutBtn = _infoLabel = nullptr;
}

void MapScreen::render() {
    if (!_canvas || !_cbuf) return;
    renderTiles();
    drawOwnMarker();
    drawContactMarker();
    drawCrosshair();
    drawScaleBar();
    lv_obj_invalidate(_canvas);
    updateZoomButtons();
    updateInfoLabel();
}

void MapScreen::renderTiles() {
    // World-pixel of centre; canvas (0,0) = topLeft.
    TileFrac f = latLonToTileXY(_lat, _lon, _zoom);
    const double cpx = f.x * (double)TILE;
    const double cpy = f.y * (double)TILE;
    const double topLeftX = cpx - CANVAS_W / 2.0;
    const double topLeftY = cpy - CANVAS_H / 2.0;

    const int txMin = (int)floor(topLeftX / (double)TILE);
    const int tyMin = (int)floor(topLeftY / (double)TILE);
    const int txMax = (int)floor((topLeftX + CANVAS_W - 1) / (double)TILE);
    const int tyMax = (int)floor((topLeftY + CANVAS_H - 1) / (double)TILE);

    const int n = slippyTileCount(_zoom);
    auto& loader = TileLoader::instance();

    for (int ty = tyMin; ty <= tyMax; ty++) {
        for (int tx = txMin; tx <= txMax; tx++) {
            const int dstX = (int)(tx * TILE - topLeftX);
            const int dstY = (int)(ty * TILE - topLeftY);

            if (ty < 0 || ty >= n) {
                // Above/below map: leave at grey (canvas starts zeroed then
                // fillGrey anyway for missing tiles).
                continue;
            }
            int wrappedTx = tx % n;
            if (wrappedTx < 0) wrappedTx += n;
            loader.decodeInto(_cbuf, CANVAS_W, CANVAS_H, dstX, dstY, _zoom, wrappedTx, ty);
        }
    }
}

static void drawDot(lv_color_t* buf, int bufW, int bufH, int cx, int cy, int r, lv_color_t c, lv_color_t outline) {
    for (int dy = -r - 1; dy <= r + 1; dy++) {
        const int y = cy + dy;
        if (y < 0 || y >= bufH) continue;
        for (int dx = -r - 1; dx <= r + 1; dx++) {
            const int x = cx + dx;
            if (x < 0 || x >= bufW) continue;
            const int d2 = dx * dx + dy * dy;
            if (d2 <= r * r) {
                buf[y * bufW + x] = c;
            } else if (d2 <= (r + 1) * (r + 1)) {
                buf[y * bufW + x] = outline;
            }
        }
    }
}

static void drawRect(lv_color_t* buf, int bufW, int bufH, int x0, int y0, int w, int h, lv_color_t c) {
    for (int y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= bufH) continue;
        for (int x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= bufW) continue;
            buf[y * bufW + x] = c;
        }
    }
}

void MapScreen::drawContactMarker() {
    // Contact is always at canvas centre (map is centred on it).
    drawDot(_cbuf, CANVAS_W, CANVAS_H, CANVAS_W / 2, CANVAS_H / 2, 5,
            theme::ACCENT, lv_color_white());
}

void MapScreen::drawOwnMarker() {
    auto& gps = GPS::instance();
    FixStatus fs = gps.fixStatus();
    if (fs == FixStatus::NO_FIX) return;

    double olat, olon;
    if (fs == FixStatus::LIVE) { olat = gps.lat();       olon = gps.lon(); }
    else                       { olat = gps.cachedLat(); olon = gps.cachedLon(); }

    TileFrac fo = latLonToTileXY(olat, olon, _zoom);
    TileFrac fc = latLonToTileXY(_lat, _lon, _zoom);
    const int dx = (int)((fo.x - fc.x) * (double)TILE);
    const int dy = (int)((fo.y - fc.y) * (double)TILE);
    const int px = CANVAS_W / 2 + dx;
    const int py = CANVAS_H / 2 + dy;
    if (px < -6 || px >= CANVAS_W + 6 || py < -6 || py >= CANVAS_H + 6) return;

    lv_color_t c = (fs == FixStatus::LIVE) ? theme::ONLINE_DOT : theme::GPS_LAST_KNOWN;
    drawDot(_cbuf, CANVAS_W, CANVAS_H, px, py, 4, c, lv_color_black());
}

void MapScreen::drawCrosshair() {
    const int cx = CANVAS_W / 2;
    const int cy = CANVAS_H / 2;
    const lv_color_t c = lv_color_white();
    // Thin crosshair 10px arms with 3px gap.
    for (int i = 3; i <= 10; i++) {
        if (cx - i >= 0)        _cbuf[cy * CANVAS_W + (cx - i)] = c;
        if (cx + i < CANVAS_W)  _cbuf[cy * CANVAS_W + (cx + i)] = c;
        if (cy - i >= 0)        _cbuf[(cy - i) * CANVAS_W + cx] = c;
        if (cy + i < CANVAS_H)  _cbuf[(cy + i) * CANVAS_W + cx] = c;
    }
}

void MapScreen::drawScaleBar() {
    const double mpp = metersPerPixel(_lat, _zoom);
    const int barLen = 50;  // pixels
    const int barX   = 4;
    const int barY   = CANVAS_H - 20;
    // Solid bar + white ends
    drawRect(_cbuf, CANVAS_W, CANVAS_H, barX, barY, barLen, 2, lv_color_white());
    drawRect(_cbuf, CANVAS_W, CANVAS_H, barX,            barY - 3, 1, 8, lv_color_white());
    drawRect(_cbuf, CANVAS_W, CANVAS_H, barX + barLen - 1, barY - 3, 1, 8, lv_color_white());

    // Label text is on _infoLabel (LVGL), redundant with scale bar — keep bar
    // as visual reference; info label shows zoom + meters/50px.
    (void)mpp;
}

void MapScreen::updateInfoLabel() {
    const double mpp = metersPerPixel(_lat, _zoom);
    const double metersPer50 = mpp * 50.0;
    char buf[48];
    if (metersPer50 >= 1000.0) {
        snprintf(buf, sizeof(buf), "z=%u  %.1f km", (unsigned)_zoom, metersPer50 / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "z=%u  %d m", (unsigned)_zoom, (int)(metersPer50 + 0.5));
    }
    lv_label_set_text(_infoLabel, buf);
    lv_obj_align(_infoLabel, LV_ALIGN_BOTTOM_LEFT, 4, -4);
}

void MapScreen::updateZoomButtons() {
    const bool canIn  = (_zoomIdx + 1) < (int)_zooms.size();
    const bool canOut = _zoomIdx > 0;
    if (canIn)  lv_obj_clear_state(_zoomInBtn,  LV_STATE_DISABLED);
    else        lv_obj_add_state(_zoomInBtn,    LV_STATE_DISABLED);
    if (canOut) lv_obj_clear_state(_zoomOutBtn, LV_STATE_DISABLED);
    else        lv_obj_add_state(_zoomOutBtn,   LV_STATE_DISABLED);
}

// --- callbacks ---

void MapScreen::closeBtnCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (self) self->close();
}

void MapScreen::zoomInCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    if (self->_zoomIdx + 1 < (int)self->_zooms.size()) {
        self->_zoomIdx++;
        self->_zoom = self->_zooms[self->_zoomIdx];
        self->render();
    }
}

void MapScreen::zoomOutCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    if (self->_zoomIdx > 0) {
        self->_zoomIdx--;
        self->_zoom = self->_zooms[self->_zoomIdx];
        self->render();
    }
}

void MapScreen::screenKeyCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    uint32_t key = lv_event_get_key(e);
    switch (key) {
        case LV_KEY_ESC:
            self->close();
            break;
        case '+': case '=':
            MapScreen::zoomInCb(e);
            break;
        case '-': case '_':
            MapScreen::zoomOutCb(e);
            break;
        default:
            break;
    }
}

}  // namespace mclite
