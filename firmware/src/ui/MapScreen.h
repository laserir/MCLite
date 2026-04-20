#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <vector>

namespace mclite {

// Full-screen map view: renders slippy tiles from SD centred on a contact's
// location, with +/-/close controls overlaid. No panning.
class MapScreen {
public:
    // Open the screen, save the previously-active screen for restore on close.
    void open(double contactLat, double contactLon, const String& contactName);

    // Close: restores the previous screen, deletes widgets and the canvas
    // buffer. Safe to call when not open.
    void close();

    bool isOpen() const { return _screen != nullptr; }

private:
    // --- lifecycle ---
    void buildWidgets();
    void destroyWidgets();
    void pickInitialZoom();

    // --- rendering ---
    void render();
    void renderTiles();
    void drawContactMarker();
    void drawOwnMarker();
    void drawScaleBar();
    void drawCrosshair();
    void updateZoomButtons();
    void updateInfoLabel();

    // --- input ---
    static void closeBtnCb(lv_event_t* e);
    static void zoomInCb(lv_event_t* e);
    static void zoomOutCb(lv_event_t* e);
    static void screenKeyCb(lv_event_t* e);

    // --- state ---
    lv_obj_t*   _screen       = nullptr;
    lv_obj_t*   _prevScreen   = nullptr;
    lv_obj_t*   _canvas       = nullptr;
    lv_color_t* _cbuf         = nullptr;
    lv_obj_t*   _closeBtn     = nullptr;
    lv_obj_t*   _zoomInBtn    = nullptr;
    lv_obj_t*   _zoomOutBtn   = nullptr;
    lv_obj_t*   _infoLabel    = nullptr;
    lv_group_t* _mapGroup     = nullptr;
    lv_group_t* _prevGroup    = nullptr;

    double   _lat = 0.0;
    double   _lon = 0.0;
    String   _contactName;

    std::vector<uint8_t> _zooms;  // snapshot from TileLoader
    int      _zoomIdx = 0;
    uint8_t  _zoom    = 0;

    static constexpr int CANVAS_W = 320;
    static constexpr int CANVAS_H = 240;
};

}  // namespace mclite
