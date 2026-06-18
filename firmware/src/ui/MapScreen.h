#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <vector>

#include "theme.h"
#include "../hal/Display.h"

namespace mclite {

// Board-specific map layout constants.
// On T-Deck the map screen is a child of _mainScreen so the status bar stays
// visible; the canvas is shifted down and shortened accordingly. On T-Watch
// the map takes over the whole display (lv_scr_load) as before.
#ifdef PLATFORM_TDECK
static constexpr int MAP_SCREEN_Y = theme::STATUS_BAR_HEIGHT;
static constexpr int MAP_CANVAS_H  = Display::height() - theme::STATUS_BAR_HEIGHT - theme::CHAT_HEADER_HEIGHT;
#else
static constexpr int MAP_SCREEN_Y = 0;
static constexpr int MAP_CANVAS_H  = Display::height();
#endif

// Full-screen map view: renders slippy tiles from SD centred on a contact's
// location, with close / zoom-in / center / zoom-out controls overlaid.
// Drag the canvas with one finger to pan; Center snaps back to the contact's
// original lat/lon.
class MapScreen {
public:
    // Open the general map focused on one contact (telemetry "Map" button):
    // centers on the contact and pre-selects it (highlight + name). Same screen
    // and controls as openGeneral(); pubKey may be null.
    void open(const uint8_t* pubKey, double contactLat, double contactLon, const String& contactName);

    // Open the general map: own location + markers for every heard node with GPS.
    // Centers on own location, else the most-recent heard-with-GPS node. No-ops
    // (no open) if there's nothing to show — caller should have checked tiles.
    void openGeneral();

    // Close: restores the previous screen, deletes widgets and the canvas
    // buffer. Safe to call when not open.
    void close();

    bool isOpen() const { return _screen != nullptr; }

private:
    // --- lifecycle ---
    void doOpen();                 // shared tail of open()/openGeneral()
    bool chooseGeneralCenter(double& lat, double& lon);  // own GPS, else a known node location
    void buildMarkers();           // gather known node locations (contacts + heard), deduped
    void buildWidgets();
    void destroyWidgets();
    void pickInitialZoom();

    // --- rendering ---
    void render();
    void recenter();               // own location if available, else the open-center
    void panBy(int dxPx, int dyPx); // shift view by pixel delta (button pan)
    void renderTiles();
    void drawHeardMarkers();       // type-letter dots for contacts + heard nodes w/ GPS
    void drawOwnMarker();
    // lat/lon -> canvas pixel; returns false if well off-canvas. Shared by draw + tap.
    bool markerScreenPos(double lat, double lon, int& px, int& py) const;
    void hitTestMarker(const lv_point_t& p);  // tap → show nearest node's name in _selLabel
    void drawScaleBar();
    void drawCrosshair();
    void updateZoomButtons();
    void updateInfoLabel();

    // --- input ---
    static void closeBtnCb(lv_event_t* e);
    static void reloadBtnCb(lv_event_t* e);   // general mode: re-scan heard nodes
    static void zoomInCb(lv_event_t* e);
    static void zoomOutCb(lv_event_t* e);
    static void centerBtnCb(lv_event_t* e);
    static void panUpCb(lv_event_t* e);
    static void panDownCb(lv_event_t* e);
    static void panLeftCb(lv_event_t* e);
    static void panRightCb(lv_event_t* e);
    static void screenKeyCb(lv_event_t* e);
    static void panPressedCb(lv_event_t* e);
    static void panPressingCb(lv_event_t* e);
    static void panReleasedCb(lv_event_t* e);

    // --- state ---
    lv_obj_t*   _screen       = nullptr;
    lv_obj_t*   _prevScreen   = nullptr;
    lv_obj_t*   _canvas       = nullptr;
    lv_color_t* _cbuf         = nullptr;
    lv_obj_t*   _closeBtn     = nullptr;
    lv_obj_t*   _reloadBtn    = nullptr;   // general mode only: re-scan heard nodes
    lv_obj_t*   _zoomInBtn    = nullptr;
    lv_obj_t*   _zoomOutBtn   = nullptr;
    lv_obj_t*   _centerBtn    = nullptr;
    lv_obj_t*   _panUpBtn     = nullptr;
    lv_obj_t*   _panDownBtn   = nullptr;
    lv_obj_t*   _panLeftBtn   = nullptr;
    lv_obj_t*   _panRightBtn  = nullptr;
    lv_obj_t*   _infoLabel    = nullptr;
    lv_obj_t*   _selLabel     = nullptr;   // tapped-marker name (general mode), hidden by default
    lv_group_t* _mapGroup     = nullptr;
    lv_group_t* _prevGroup    = nullptr;

    bool        _general      = false;     // true = general map (heard markers), false = single contact

    // Known node locations for the general map (rebuilt each render). Sources:
    // mesh contacts (fresh telemetry location, else advert GPS) + heard adverts
    // with GPS, deduped by pubkey.
    struct MapMarker { double lat; double lon; uint8_t type; bool isContact; char name[32]; uint8_t key[32]; };
    std::vector<MapMarker> _markers;

    // Tapped/focused marker (highlighted with a ring + bigger dot while its name
    // shows in _selLabel). Keyed by pubkey (matches _markers[].key).
    bool    _hasSel = false;
    uint8_t _selKey[32] = {0};

    // Focus-on-a-contact (telemetry "Map" button): ensures the contact appears as
    // a marker even if it isn't in the contact/heard caches at open time.
    bool     _hasFocus  = false;
    double   _focusLat  = 0.0;
    double   _focusLon  = 0.0;
    uint8_t  _focusType = 0;       // ADV_TYPE_*

    // Original contact/center location — set in open()/openGeneral(), used as the
    // Center-button fallback before an own fix. Constant for the screen's life.
    double   _contactLat = 0.0;
    double   _contactLon = 0.0;
    String   _contactName;

    // Current viewport centre — starts at the contact location, mutated by
    // drag-to-pan and reset by the Center button.
    double   _centerLat = 0.0;
    double   _centerLon = 0.0;

    // Pan-gesture state.
    bool        _panActive    = false;
    bool        _panMoved     = false;  // crossed the tap-slop dead-zone → it's a pan, not a tap
    lv_point_t  _panLast{0, 0};
    lv_point_t  _panStart{0, 0};   // press point, for tap-vs-pan disambiguation
    uint32_t    _lastRenderMs = 0;

    std::vector<uint8_t> _zooms;  // snapshot from TileLoader
    int      _zoomIdx = 0;
    uint8_t  _zoom    = 0;

    static constexpr int CANVAS_W = Display::width();
    static constexpr int CANVAS_H = MAP_CANVAS_H;
};

}  // namespace mclite
