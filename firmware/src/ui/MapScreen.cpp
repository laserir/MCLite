#include "MapScreen.h"
#include "util/log.h"
#include "UIManager.h"
#include "theme.h"
#include "../storage/TileLoader.h"
#include "../storage/HeardAdvertCache.h"
#include "../storage/TelemetryCache.h"
#include "../util/ContactLocation.h"
#include "../mesh/MeshManager.h"
#include "../mesh/MCLiteMesh.h"
#include "../util/slippy.h"
#include "../hal/GPS.h"
#include "../i18n/I18n.h"
#include <helpers/AdvertDataHelpers.h>   // ADV_TYPE_*
#include <cstring>
#ifdef PLATFORM_TDECK
#include "../input/Keyboard.h"
#include "../input/Trackball.h"
#endif
#include <math.h>
#include <algorithm>
#include <esp_heap_caps.h>

namespace mclite {

static constexpr int TILE = SLIPPY_TILE_SIZE;  // 256

// Touch-button sizing: finger-friendly on T-Watch, compact on T-Deck.
#ifdef PLATFORM_TWATCH
static constexpr int MAP_BTN          = 56;
// MapScreen takes over the whole display via lv_scr_load(), so it doesn't
// benefit from the status bar / footer absorbing the rounded-corner safe
// area. Inset corner-anchored widgets explicitly so the close button and
// info label clear the bezel curve.
static constexpr int MAP_CORNER_INSET = 36;
#define MAP_BTN_FONT FONT_HEADING
// Movement (px) below which a press is a tap, not a pan. Larger on the T-Watch
// touch panel where a fingertip tap jitters more.
static constexpr int MAP_TAP_SLOP     = 16;
#else
static constexpr int MAP_BTN          = 32;
static constexpr int MAP_CORNER_INSET = 0;
#define MAP_BTN_FONT FONT_NORMAL
static constexpr int MAP_TAP_SLOP     = 10;
#endif

// Marker symbol + color + word per advert type — kept identical to the
// heard-adverts list (HeardAdvertsScreen.cpp typeIcon()/colors).
static const char* mapTypeLetter(uint8_t type) {
    switch (type) {
        case ADV_TYPE_CHAT:     return "@";
        case ADV_TYPE_REPEATER: return "P";
        case ADV_TYPE_ROOM:     return "R";
        case ADV_TYPE_SENSOR:   return "S";
        default:                return "?";
    }
}
static lv_color_t mapTypeColor(uint8_t type) {
    switch (type) {
        case ADV_TYPE_CHAT:     return theme::ACCENT();
        case ADV_TYPE_REPEATER: return theme::TEXT_PRIMARY();
        case ADV_TYPE_ROOM:     return theme::ROOM_ACCENT();
        case ADV_TYPE_SENSOR:   return theme::OFFGRID_ACCENT();
        default:                return theme::TEXT_PRIMARY();
    }
}
static const char* mapTypeWord(uint8_t type) {
    switch (type) {
        case ADV_TYPE_CHAT:     return t("heard_type_chat");
        case ADV_TYPE_REPEATER: return t("heard_type_repeater");
        case ADV_TYPE_ROOM:     return t("heard_type_room");
        case ADV_TYPE_SENSOR:   return t("heard_type_sensor");
        default:                return "";
    }
}

void MapScreen::open(const uint8_t* pubKey, double contactLat, double contactLon,
                     const String& contactName) {
    if (_screen) return;  // already open

    // Same general map, just opened focused on one contact: center on it,
    // pre-select it (highlight ring + bigger dot + name in the bottom label),
    // and guarantee it shows even if it isn't in the contact/heard caches yet.
    _general     = true;
    _contactName = contactName;
    _contactLat  = contactLat;   // Center-button fallback before an own fix
    _contactLon  = contactLon;
    _centerLat   = contactLat;
    _centerLon   = contactLon;
    _hasSel      = true;
    if (pubKey) memcpy(_selKey, pubKey, 32); else memset(_selKey, 0, 32);
    _hasFocus    = true;
    _focusLat    = contactLat;
    _focusLon    = contactLon;
    _focusType   = ADV_TYPE_CHAT;  // telemetry "Map" is a chat-contact action
    doOpen();
    if (!_screen) return;          // no tiles → doOpen bailed

    // Show the focused contact's name immediately (use its real marker type/name
    // if it ended up in _markers, else the passed-in name).
    if (_selLabel) {
        const char* word = mapTypeWord(_focusType);
        String nm = _contactName;
        for (const auto& m : _markers) {
            if (memcmp(m.key, _selKey, 32) == 0) { word = mapTypeWord(m.type); nm = m.name; break; }
        }
        String s = String(word) + " " + nm;
        lv_label_set_text(_selLabel, s.c_str());
        lv_obj_clear_flag(_selLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(_selLabel, LV_ALIGN_BOTTOM_MID, 0, -(MAP_CORNER_INSET + theme::PAD_SMALL));
    }
}

// Collect every node location we know: mesh contacts (fresh telemetry location,
// else their advert GPS) plus heard-advert entries with GPS, deduped by pubkey.
void MapScreen::buildMarkers() {
    _markers.clear();
    auto seen = [&](const uint8_t* k) {
        for (auto& m : _markers) if (memcmp(m.key, k, 32) == 0) return true;
        return false;
    };
    auto add = [&](double lat, double lon, uint8_t type, bool isContact,
                   const char* name, const uint8_t* key) {
        MapMarker m; m.lat = lat; m.lon = lon; m.type = type; m.isContact = isContact;
        strncpy(m.name, name ? name : "", sizeof(m.name) - 1); m.name[sizeof(m.name) - 1] = 0;
        memcpy(m.key, key, 32);
        _markers.push_back(m);
    };

    // 1) Contacts — best-known position (fresh telemetry, else advert/heard GPS),
    // via the shared accessor so map/badge/modal all agree.
    MCLiteMesh* mesh = MeshManager::instance().mesh();
    if (mesh) {
        int n = mesh->getNumContacts();
        for (int i = 0; i < n; i++) {
            ContactInfo* c = mesh->getContactByIdx(i);
            if (!c) continue;
            ContactLocation loc = bestKnownLocation(c->id.pub_key);
            if (loc.valid) add(loc.lat, loc.lon, c->type, true, c->name, c->id.pub_key);
        }
    }

    // 2) Heard adverts with GPS that aren't already represented by a contact.
    auto& cache = HeardAdvertCache::instance();
    const HeardAdvert* es = cache.entries();
    for (int i = 0; i < cache.count(); i++) {
        if (!es[i].hasGps || seen(es[i].pubKey)) continue;
        add(es[i].gpsLat / 1e6, es[i].gpsLon / 1e6, es[i].type, false, es[i].name, es[i].pubKey);
    }

    // 3) When opened focused on a contact (telemetry "Map" button), make sure it
    // shows even if it isn't a known contact/heard node right now.
    if (_hasFocus && !seen(_selKey)) {
        add(_focusLat, _focusLon, _focusType, true, _contactName.c_str(), _selKey);
    }
}

bool MapScreen::chooseGeneralCenter(double& lat, double& lon) {
    auto& gps = GPS::instance();
    FixStatus fs = gps.fixStatus();
    if (fs == FixStatus::LIVE)       { lat = gps.lat();       lon = gps.lon();       return true; }
    if (fs == FixStatus::LAST_KNOWN) { lat = gps.cachedLat(); lon = gps.cachedLon(); return true; }
    // No own fix → center on the first known node location, if any.
    buildMarkers();
    if (_markers.empty()) return false;
    lat = _markers[0].lat;
    lon = _markers[0].lon;
    return true;
}

void MapScreen::openGeneral() {
    if (_screen) return;
    double clat, clon;
    if (!chooseGeneralCenter(clat, clon)) {
        UIManager::instance().showToast(t("map_no_locations"));
        return;
    }
    _general     = true;
    _hasSel      = false;
    _hasFocus    = false;
    _contactName = "";
    _contactLat  = clat;   // doubles as the Center-button fallback before an own fix
    _contactLon  = clon;
    _centerLat   = clat;
    _centerLon   = clon;
    doOpen();
}

void MapScreen::doOpen() {
    // Snapshot available zooms and pick an initial level before building the
    // canvas — if tiles are unavailable we bail out without allocating.
    _zooms = TileLoader::instance().availableZooms();
    if (_zooms.empty()) {
        LOGLN("[MapScreen] no tiles available; aborting open");
        return;
    }
    pickInitialZoom();

    // Canvas buffer lives in PSRAM and is reserved ONCE per device lifetime
    // — re-malloc on every open() can fail when PSRAM is fragmented (a
    // 410×502×2 = 412 KB contiguous block isn't trivial after the device has
    // been running a while). heap_caps_malloc with explicit SPIRAM cap also
    // avoids ps_malloc's silent fall-through to internal RAM that wouldn't
    // fit anyway.
    static lv_color_t* s_cbuf = nullptr;
    const size_t bytes = (size_t)CANVAS_W * (size_t)CANVAS_H * sizeof(lv_color_t);
    if (!s_cbuf) {
        s_cbuf = (lv_color_t*)heap_caps_malloc(bytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_cbuf) {
        LOGF("[MapScreen] PSRAM alloc failed (%u B); free SPIRAM=%u "
                      "largest=%u\n", (unsigned)bytes,
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        return;
    }
    _cbuf = s_cbuf;

#ifndef PLATFORM_TDECK
    _prevScreen = lv_scr_act();
#endif
    buildWidgets();

#ifdef PLATFORM_TDECK
    // Swap input group on T-Deck so the trackball/keyboard target the map
    // buttons; T-Watch is touch-only and doesn't need group navigation.
    _prevGroup = UIManager::instance().inputGroup();
    _mapGroup = lv_group_create();
    lv_group_add_obj(_mapGroup, _closeBtn);
    if (_reloadBtn) lv_group_add_obj(_mapGroup, _reloadBtn);
    lv_group_add_obj(_mapGroup, _zoomInBtn);
    lv_group_add_obj(_mapGroup, _zoomOutBtn);
    lv_group_add_obj(_mapGroup, _panUpBtn);
    lv_group_add_obj(_mapGroup, _panLeftBtn);
    lv_group_add_obj(_mapGroup, _centerBtn);
    lv_group_add_obj(_mapGroup, _panRightBtn);
    lv_group_add_obj(_mapGroup, _panDownBtn);
    lv_group_focus_obj(_closeBtn);
    if (Keyboard::instance().indev())
        lv_indev_set_group(Keyboard::instance().indev(), _mapGroup);
    if (Trackball::instance().indev())
        lv_indev_set_group(Trackball::instance().indev(), _mapGroup);
#endif

#ifdef PLATFORM_TDECK
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
#else
    lv_scr_load(_screen);
#endif
    render();
}

void MapScreen::close() {
    if (!_screen) return;

#ifdef PLATFORM_TDECK
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
#endif

    // Restore previous screen and delete ours.
#ifdef PLATFORM_TDECK
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
#else
    if (_prevScreen && _prevScreen != _screen) {
        lv_scr_load(_prevScreen);
    }
#endif
    destroyWidgets();

    // Don't free _cbuf — it's a shared PSRAM reservation owned by open()'s
    // static slot. Just drop our pointer so isOpen() returns false correctly.
    _cbuf = nullptr;
    _prevScreen = nullptr;
    _prevGroup = nullptr;
    _zooms.clear();
    LOGLN("[MapScreen] closed");
}

void MapScreen::pickInitialZoom() {
    // Pick the largest available zoom where the centre tile exists; fall back
    // to the largest available zoom if no centre tile is present.
    auto& loader = TileLoader::instance();
    int chosen = (int)_zooms.size() - 1;
    for (int i = (int)_zooms.size() - 1; i >= 0; i--) {
        uint8_t z = _zooms[i];
        TileFrac f = latLonToTileXY(_contactLat, _contactLon, z);
        int tx = (int)floor(f.x);
        int ty = (int)floor(f.y);
        if (loader.tileExists(z, tx, ty)) { chosen = i; break; }
    }
    _zoomIdx = chosen;
    _zoom = _zooms[_zoomIdx];
}

void MapScreen::buildWidgets() {
#ifdef PLATFORM_TDECK
    _screen = lv_win_create(UIManager::instance().mainScreen(), theme::CHAT_HEADER_HEIGHT);
    lv_obj_set_size(_screen, Display::width(), Display::height() - theme::STATUS_BAR_HEIGHT);
    lv_obj_align(_screen, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_color(_screen, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_set_style_radius(_screen, 0, 0);
    lv_obj_set_style_pad_all(_screen, 0, 0);

    // Style the header
    lv_obj_t* header = lv_win_get_header(_screen);
    lv_obj_set_style_bg_color(header, theme::BG_STATUS_BAR(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_hor(header, theme::CHAT_HEADER_PAD_HOR, 0);

    // Back button (replaces the floating close button)
    _closeBtn = lv_win_add_btn(_screen, LV_SYMBOL_LEFT, theme::BTN_HEADER_BACK_W);
    lv_obj_set_style_bg_opa(_closeBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(_closeBtn, 0, 0);
    lv_obj_set_style_border_width(_closeBtn, 0, 0);
    lv_obj_add_event_cb(_closeBtn, &MapScreen::closeBtnCb, LV_EVENT_CLICKED, this);
    lv_obj_t* backLbl = lv_obj_get_child(_closeBtn, 0);
    lv_obj_set_style_text_font(backLbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(backLbl, theme::ACCENT(), 0);

    // Title
    lv_obj_t* title = lv_win_add_title(_screen, t("map_title"));
    lv_obj_set_style_text_font(title, FONT_HEADING, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY(), 0);

    // Header icon buttons: reload (general only), zoom out, zoom in
    auto makeHeaderBtn = [this](const char* sym, lv_event_cb_t cb) -> lv_obj_t* {
        lv_obj_t* btn = lv_win_add_btn(_screen, sym, theme::BTN_HEADER_ICON_W);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
        return btn;
    };

    if (_general) {
        _reloadBtn = makeHeaderBtn(LV_SYMBOL_REFRESH, &MapScreen::reloadBtnCb);
    }
    _zoomOutBtn = makeHeaderBtn(LV_SYMBOL_MINUS, &MapScreen::zoomOutCb);
    _zoomInBtn = makeHeaderBtn(LV_SYMBOL_PLUS, &MapScreen::zoomInCb);

    // Content area — canvas and floating controls live here
    lv_obj_t* content = lv_win_get_content(_screen);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Canvas (full content area). Clickable so it can swallow PRESS events
    // for drag-to-pan; buttons sit z-above and capture their own taps.
    _canvas = lv_canvas_create(content);
    lv_canvas_set_buffer(_canvas, _cbuf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(_canvas, 0, 0);
    lv_obj_add_flag(_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_canvas, &MapScreen::panPressedCb,  LV_EVENT_PRESSED,  this);
    lv_obj_add_event_cb(_canvas, &MapScreen::panPressingCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_canvas, &MapScreen::panReleasedCb, LV_EVENT_RELEASED, this);

    auto styleBtn = [](lv_obj_t* b) {
        lv_obj_set_size(b, MAP_BTN, MAP_BTN);
        lv_obj_set_style_radius(b, MAP_BTN / 2, 0);
        lv_obj_set_style_bg_color(b, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_style_pad_all(b, 0, 0);
    };

    auto styleLbl = [](lv_obj_t* lbl) {
        lv_obj_set_style_text_font(lbl, MAP_BTN_FONT, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
        lv_obj_center(lbl);
    };

    // Bottom-right pan cluster: plus shape around center button.
    _panUpBtn = lv_btn_create(content);
    styleBtn(_panUpBtn);
    lv_obj_align(_panUpBtn, LV_ALIGN_BOTTOM_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + 2*MAP_BTN + 2*theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_panUpBtn);
        lv_label_set_text(lbl, LV_SYMBOL_UP);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_panUpBtn, &MapScreen::panUpCb, LV_EVENT_CLICKED, this);

    _panLeftBtn = lv_btn_create(content);
    styleBtn(_panLeftBtn);
    lv_obj_align(_panLeftBtn, LV_ALIGN_BOTTOM_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + 2*MAP_BTN + 2*theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_panLeftBtn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_panLeftBtn, &MapScreen::panLeftCb, LV_EVENT_CLICKED, this);

    _centerBtn = lv_btn_create(content);
    styleBtn(_centerBtn);
    lv_obj_align(_centerBtn, LV_ALIGN_BOTTOM_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_centerBtn);
        lv_label_set_text(lbl, LV_SYMBOL_GPS);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_centerBtn, &MapScreen::centerBtnCb, LV_EVENT_CLICKED, this);

    _panRightBtn = lv_btn_create(content);
    styleBtn(_panRightBtn);
    lv_obj_align(_panRightBtn, LV_ALIGN_BOTTOM_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_panRightBtn);
        lv_label_set_text(lbl, LV_SYMBOL_RIGHT);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_panRightBtn, &MapScreen::panRightCb, LV_EVENT_CLICKED, this);

    _panDownBtn = lv_btn_create(content);
    styleBtn(_panDownBtn);
    lv_obj_align(_panDownBtn, LV_ALIGN_BOTTOM_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_panDownBtn);
        lv_label_set_text(lbl, LV_SYMBOL_DOWN);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_panDownBtn, &MapScreen::panDownCb, LV_EVENT_CLICKED, this);

    // Info label (bottom-left): zoom level + scale text.
    _infoLabel = lv_label_create(content);
    lv_obj_set_style_text_font(_infoLabel, FONT_BODY, 0);
    lv_obj_set_style_text_color(_infoLabel, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_bg_color(_infoLabel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(_infoLabel, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(_infoLabel, 3, 0);
    lv_obj_align(_infoLabel, LV_ALIGN_BOTTOM_LEFT,
                  (MAP_CORNER_INSET + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL));

    // Selection label (bottom-centre): shows a tapped node's name in general
    // mode. Hidden until a marker is tapped; render() never overwrites it.
    _selLabel = lv_label_create(content);
    lv_obj_set_style_text_font(_selLabel, FONT_BODY, 0);
    lv_obj_set_style_text_color(_selLabel, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_bg_color(_selLabel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(_selLabel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_selLabel, 4, 0);
    lv_obj_set_style_radius(_selLabel, 4, 0);
    lv_obj_align(_selLabel, LV_ALIGN_BOTTOM_MID, 0, -(MAP_CORNER_INSET + theme::PAD_SMALL));
    lv_obj_add_flag(_selLabel, LV_OBJ_FLAG_HIDDEN);

#else  // PLATFORM_TWATCH
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, lv_color_black(), 0);
    lv_obj_set_style_radius(_screen, 0, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(_screen, 0, 0);

    // Canvas (full-screen). Clickable so the canvas can swallow PRESS events
    // for drag-to-pan; buttons sit z-above and capture their own taps.
    _canvas = lv_canvas_create(_screen);
    lv_canvas_set_buffer(_canvas, _cbuf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(_canvas, 0, 0);
    lv_obj_add_flag(_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_canvas, &MapScreen::panPressedCb,  LV_EVENT_PRESSED,  this);
    lv_obj_add_event_cb(_canvas, &MapScreen::panPressingCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_canvas, &MapScreen::panReleasedCb, LV_EVENT_RELEASED, this);

    auto styleBtn = [](lv_obj_t* b) {
        lv_obj_set_size(b, MAP_BTN, MAP_BTN);
        lv_obj_set_style_radius(b, MAP_BTN / 2, 0);
        lv_obj_set_style_bg_color(b, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_style_pad_all(b, 0, 0);
#ifdef PLATFORM_TWATCH
        lv_obj_set_ext_click_area(b, 8);
#endif
    };

    auto styleLbl = [](lv_obj_t* lbl) {
        lv_obj_set_style_text_font(lbl, MAP_BTN_FONT, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
        lv_obj_center(lbl);
    };

    // Top-left: close button.
    _closeBtn = lv_btn_create(_screen);
    styleBtn(_closeBtn);
    lv_obj_align(_closeBtn, LV_ALIGN_TOP_LEFT,
                  (MAP_CORNER_INSET + theme::PAD_SMALL),
                  (MAP_CORNER_INSET + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_closeBtn);
        lv_label_set_text(lbl, LV_SYMBOL_CLOSE);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_closeBtn, &MapScreen::closeBtnCb, LV_EVENT_CLICKED, this);

    if (_general) {
        _reloadBtn = lv_btn_create(_screen);
        styleBtn(_reloadBtn);
        lv_obj_align(_reloadBtn, LV_ALIGN_TOP_RIGHT,
                     -(MAP_CORNER_INSET + theme::PAD_SMALL + 2*(MAP_BTN + theme::PAD_SMALL)),
                      (MAP_CORNER_INSET + theme::PAD_SMALL));
        {
            lv_obj_t* lbl = lv_label_create(_reloadBtn);
            lv_label_set_text(lbl, LV_SYMBOL_REFRESH);
            styleLbl(lbl);
        }
        lv_obj_add_event_cb(_reloadBtn, &MapScreen::reloadBtnCb, LV_EVENT_CLICKED, this);
    }

    _zoomInBtn = lv_btn_create(_screen);
    styleBtn(_zoomInBtn);
    lv_obj_align(_zoomInBtn, LV_ALIGN_TOP_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL),
                  (MAP_CORNER_INSET + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_zoomInBtn);
        lv_label_set_text(lbl, LV_SYMBOL_PLUS);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_zoomInBtn, &MapScreen::zoomInCb, LV_EVENT_CLICKED, this);

    // Top-right row: zoom out, zoom in, and reload (general mode).
    _zoomOutBtn = lv_btn_create(_screen);
    styleBtn(_zoomOutBtn);
    lv_obj_align(_zoomOutBtn, LV_ALIGN_TOP_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL),
                  (MAP_CORNER_INSET + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_zoomOutBtn);
        lv_label_set_text(lbl, LV_SYMBOL_MINUS);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_zoomOutBtn, &MapScreen::zoomOutCb, LV_EVENT_CLICKED, this);

    // Bottom-right pan cluster: plus shape around center button.
    _panUpBtn = lv_btn_create(_screen);
    styleBtn(_panUpBtn);
    lv_obj_align(_panUpBtn, LV_ALIGN_BOTTOM_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + 2*MAP_BTN + 2*theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_panUpBtn);
        lv_label_set_text(lbl, LV_SYMBOL_UP);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_panUpBtn, &MapScreen::panUpCb, LV_EVENT_CLICKED, this);

    _panLeftBtn = lv_btn_create(_screen);
    styleBtn(_panLeftBtn);
    lv_obj_align(_panLeftBtn, LV_ALIGN_BOTTOM_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + 2*MAP_BTN + 2*theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_panLeftBtn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_panLeftBtn, &MapScreen::panLeftCb, LV_EVENT_CLICKED, this);

    _centerBtn = lv_btn_create(_screen);
    styleBtn(_centerBtn);
    lv_obj_align(_centerBtn, LV_ALIGN_BOTTOM_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_centerBtn);
        lv_label_set_text(lbl, LV_SYMBOL_GPS);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_centerBtn, &MapScreen::centerBtnCb, LV_EVENT_CLICKED, this);

    _panRightBtn = lv_btn_create(_screen);
    styleBtn(_panRightBtn);
    lv_obj_align(_panRightBtn, LV_ALIGN_BOTTOM_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_panRightBtn);
        lv_label_set_text(lbl, LV_SYMBOL_RIGHT);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_panRightBtn, &MapScreen::panRightCb, LV_EVENT_CLICKED, this);

    _panDownBtn = lv_btn_create(_screen);
    styleBtn(_panDownBtn);
    lv_obj_align(_panDownBtn, LV_ALIGN_BOTTOM_RIGHT,
                 -(MAP_CORNER_INSET + theme::PAD_SMALL + MAP_BTN + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL));
    {
        lv_obj_t* lbl = lv_label_create(_panDownBtn);
        lv_label_set_text(lbl, LV_SYMBOL_DOWN);
        styleLbl(lbl);
    }
    lv_obj_add_event_cb(_panDownBtn, &MapScreen::panDownCb, LV_EVENT_CLICKED, this);

    // Info label (bottom-left): zoom level + scale text.
    _infoLabel = lv_label_create(_screen);
    lv_obj_set_style_text_font(_infoLabel, FONT_BODY, 0);
    lv_obj_set_style_text_color(_infoLabel, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_bg_color(_infoLabel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(_infoLabel, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(_infoLabel, 3, 0);
    lv_obj_align(_infoLabel, LV_ALIGN_BOTTOM_LEFT,
                  (MAP_CORNER_INSET + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL));

    // Selection label (bottom-centre): shows a tapped node's name in general
    // mode. Hidden until a marker is tapped; render() never overwrites it.
    _selLabel = lv_label_create(_screen);
    lv_obj_set_style_text_font(_selLabel, FONT_BODY, 0);
    lv_obj_set_style_text_color(_selLabel, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_bg_color(_selLabel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(_selLabel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_selLabel, 4, 0);
    lv_obj_set_style_radius(_selLabel, 4, 0);
    lv_obj_align(_selLabel, LV_ALIGN_BOTTOM_MID, 0, -(MAP_CORNER_INSET + theme::PAD_SMALL));
    lv_obj_add_flag(_selLabel, LV_OBJ_FLAG_HIDDEN);
#endif

    // Screen-level key handler for Esc / +/- shortcuts (T-Deck keyboard).
    lv_obj_add_event_cb(_screen, &MapScreen::screenKeyCb, LV_EVENT_KEY, this);
}

void MapScreen::destroyWidgets() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _canvas = _closeBtn = _reloadBtn = _zoomInBtn = _zoomOutBtn = _centerBtn = _panUpBtn = _panDownBtn = _panLeftBtn = _panRightBtn = _infoLabel = _selLabel = nullptr;
}

void MapScreen::render() {
    if (!_canvas || !_cbuf) return;
    renderTiles();
    drawOwnMarker();
    buildMarkers();
    drawHeardMarkers();
    drawCrosshair();
    drawScaleBar();
    lv_obj_invalidate(_canvas);
    updateZoomButtons();
    updateInfoLabel();
    _lastRenderMs = millis();
}

void MapScreen::renderTiles() {
    // World-pixel of viewport centre; canvas (0,0) = topLeft.
    TileFrac f = latLonToTileXY(_centerLat, _centerLon, _zoom);
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

            if (ty < 0 || ty >= n) continue;
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


bool MapScreen::markerScreenPos(double lat, double lon, int& px, int& py) const {
    TileFrac fm = latLonToTileXY(lat, lon, _zoom);
    TileFrac fc = latLonToTileXY(_centerLat, _centerLon, _zoom);
    // lround (not truncation) so marker pixels track the tile blit exactly and
    // don't jitter off by a pixel across zoom levels.
    px = CANVAS_W / 2 + (int)lround((fm.x - fc.x) * (double)TILE);
    py = CANVAS_H / 2 + (int)lround((fm.y - fc.y) * (double)TILE);
    // Generous off-canvas margin (one glyph + selection-ring radius) so a marker
    // sitting near the viewport edge isn't dropped a zoom step before its tile.
    return (px >= -24 && px < CANVAS_W + 24 && py >= -24 && py < CANVAS_H + 24);
}

// Filled annulus of width `th` ending at radius r (selection highlight).
static void drawRing(lv_color_t* buf, int bufW, int bufH, int cx, int cy, int r, int th, lv_color_t c) {
    int rin = r - th; if (rin < 0) rin = 0;
    const int r0 = rin * rin, r1 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        const int y = cy + dy; if (y < 0 || y >= bufH) continue;
        for (int dx = -r; dx <= r; dx++) {
            const int x = cx + dx; if (x < 0 || x >= bufW) continue;
            const int d2 = dx * dx + dy * dy;
            if (d2 >= r0 && d2 <= r1) buf[y * bufW + x] = c;
        }
    }
}

void MapScreen::drawHeardMarkers() {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = FONT_BODY;   // readable on both the T-Deck and the large T-Watch AMOLED
    for (const auto& m : _markers) {
        int px, py;
        if (!markerScreenPos(m.lat, m.lon, px, py)) continue;

        // Each marker is a filled dot in its type color (blue = a saved contact,
        // grey = a heard stranger for chat; list colors otherwise) with a black
        // rim, so the symbol reads against any map tile. The selected/focused
        // marker is drawn a bit larger and wrapped in a bold ring.
        bool sel = _hasSel && memcmp(m.key, _selKey, 32) == 0;
        lv_color_t dotColor = (m.type == ADV_TYPE_CHAT)
            ? (m.isContact ? theme::ACCENT() : theme::TEXT_SECONDARY())
            : mapTypeColor(m.type);
        drawDot(_cbuf, CANVAS_W, CANVAS_H, px, py, sel ? 10 : 8, dotColor, lv_color_black());

        if (sel) {
            // Bold white ring + black halos, sitting just outside the larger dot.
            drawRing(_cbuf, CANVAS_W, CANVAS_H, px, py, 15, 1, lv_color_black());  // outer halo
            drawRing(_cbuf, CANVAS_W, CANVAS_H, px, py, 14, 3, lv_color_white());  // bold ring
            drawRing(_cbuf, CANVAS_W, CANVAS_H, px, py, 12, 1, lv_color_black());  // inner halo
        }

        // Symbol on top, in black or white — whichever contrasts with the dot.
        dsc.color = (lv_color_brightness(dotColor) > 128) ? lv_color_black() : lv_color_white();
        // Roughly center the single glyph on the coordinate.
        lv_canvas_draw_text(_canvas, px - 5, py - 9, 16, &dsc, mapTypeLetter(m.type));
    }
}

void MapScreen::drawOwnMarker() {
    auto& gps = GPS::instance();
    FixStatus fs = gps.fixStatus();
    if (fs == FixStatus::NO_FIX) return;

    double olat, olon;
    if (fs == FixStatus::LIVE) { olat = gps.lat();       olon = gps.lon(); }
    else                       { olat = gps.cachedLat(); olon = gps.cachedLon(); }

    TileFrac fo = latLonToTileXY(olat, olon, _zoom);
    TileFrac fc = latLonToTileXY(_centerLat, _centerLon, _zoom);
    const int dx = (int)lround((fo.x - fc.x) * (double)TILE);
    const int dy = (int)lround((fo.y - fc.y) * (double)TILE);
    const int px = CANVAS_W / 2 + dx;
    const int py = CANVAS_H / 2 + dy;
    if (px < -6 || px >= CANVAS_W + 6 || py < -6 || py >= CANVAS_H + 6) return;

    lv_color_t c = (fs == FixStatus::LIVE) ? theme::ONLINE_DOT() : theme::GPS_LAST_KNOWN();
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
    const int barLen = 50;  // pixels
    const int barX   = 4;
    const int barY   = CANVAS_H - 20;
    drawRect(_cbuf, CANVAS_W, CANVAS_H, barX, barY, barLen, 2, lv_color_white());
    drawRect(_cbuf, CANVAS_W, CANVAS_H, barX,              barY - 3, 1, 8, lv_color_white());
    drawRect(_cbuf, CANVAS_W, CANVAS_H, barX + barLen - 1, barY - 3, 1, 8, lv_color_white());
}

void MapScreen::updateInfoLabel() {
    const double mpp = metersPerPixel(_centerLat, _zoom);
    const double metersPer50 = mpp * 50.0;
    char buf[48];
    if (metersPer50 >= 1000.0) {
        snprintf(buf, sizeof(buf), "z=%u  %.1f km", (unsigned)_zoom, metersPer50 / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "z=%u  %d m", (unsigned)_zoom, (int)(metersPer50 + 0.5));
    }
    lv_label_set_text(_infoLabel, buf);
    lv_obj_align(_infoLabel, LV_ALIGN_BOTTOM_LEFT,
                  (MAP_CORNER_INSET + theme::PAD_SMALL),
                 -(MAP_CORNER_INSET + theme::PAD_SMALL));
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

void MapScreen::reloadBtnCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    // Re-scan heard/contact nodes AND re-check our own position: render() rebuilds
    // the markers and redraws the own dot from the live GPS fix, so a position that
    // became available since opening now shows (and the Center button will use it).
    // The viewport is left where it is — Center is what moves it.
    self->render();
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

void MapScreen::centerBtnCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->recenter();
}

void MapScreen::panBy(int dxPx, int dyPx) {
    // Shift the view by a pixel delta. Positive dxPx pans right, positive dyPx pans down.
    const double s = 360.0 / (256.0 * (double)(1 << _zoom));
    const double cosLat = cos(_centerLat * M_PI / 180.0);
    _centerLon += -(double)dxPx * s;
    _centerLat += (double)dyPx * s * cosLat;

    if (_centerLat >  SLIPPY_LAT_MAX) _centerLat =  SLIPPY_LAT_MAX;
    if (_centerLat < -SLIPPY_LAT_MAX) _centerLat = -SLIPPY_LAT_MAX;
    while (_centerLon >= 180.0) _centerLon -= 360.0;
    while (_centerLon < -180.0) _centerLon += 360.0;

    render();
}

void MapScreen::panUpCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->panBy(0, CANVAS_H / 2);
}

void MapScreen::panDownCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->panBy(0, -CANVAS_H / 2);
}

void MapScreen::panLeftCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->panBy(CANVAS_W / 2, 0);
}

void MapScreen::panRightCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->panBy(-CANVAS_W / 2, 0);
}

// Center on our own location if a fix is available (checked live, so it works as
// soon as GPS locks). Otherwise fall back to the location the map opened on
// (_contactLat/Lon — the contact for a focused open, or the chosen node for the
// general map). Used by the Center button and by Reload.
void MapScreen::recenter() {
    auto& gps = GPS::instance();
    FixStatus fs = gps.fixStatus();
    if (fs == FixStatus::LIVE)            { _centerLat = gps.lat();       _centerLon = gps.lon(); }
    else if (fs == FixStatus::LAST_KNOWN) { _centerLat = gps.cachedLat(); _centerLon = gps.cachedLon(); }
    else                                  { _centerLat = _contactLat;     _centerLon = _contactLon; }
    render();
}

void MapScreen::hitTestMarker(const lv_point_t& p) {
    if (!_selLabel) return;
    int best = -1, bestD2 = 20 * 20;   // tap tolerance (px²) — fingertip-friendly
    for (size_t i = 0; i < _markers.size(); i++) {
        int px, py;
        if (!markerScreenPos(_markers[i].lat, _markers[i].lon, px, py)) continue;
        int d2 = (p.x - px) * (p.x - px) + (p.y - py) * (p.y - py);
        if (d2 <= bestD2) { bestD2 = d2; best = (int)i; }
    }
    if (best < 0) {
        _hasSel = false;
        lv_obj_add_flag(_selLabel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    memcpy(_selKey, _markers[best].key, 32);   // highlight this marker with a ring
    _hasSel = true;
    String s = String(mapTypeWord(_markers[best].type)) + " " + _markers[best].name;
    lv_label_set_text(_selLabel, s.c_str());
    lv_obj_clear_flag(_selLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(_selLabel, LV_ALIGN_BOTTOM_MID, 0, -(MAP_CORNER_INSET + theme::PAD_SMALL));
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

// --- pan handlers ---

void MapScreen::panPressedCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_indev_get_point(indev, &self->_panLast);
    self->_panStart = self->_panLast;   // remember press point for tap detection
    self->_panActive = true;
    self->_panMoved  = false;
}

void MapScreen::panPressingCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_panActive) return;
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t now;
    lv_indev_get_point(indev, &now);

    // Tap-slop dead-zone: until the finger travels past the slop from the press
    // point, treat it as a still tap — don't pan (which would shift the map and
    // steal the tap from hit-testing). Once exceeded, it's a pan for good.
    if (!self->_panMoved) {
        int sdx = now.x - self->_panStart.x;
        int sdy = now.y - self->_panStart.y;
        if (sdx * sdx + sdy * sdy <= MAP_TAP_SLOP * MAP_TAP_SLOP) {
            self->_panLast = now;   // track so the first real pan delta isn't a jump
            return;
        }
        self->_panMoved = true;
    }

    int dxPx = now.x - self->_panLast.x;
    int dyPx = now.y - self->_panLast.y;
    if (dxPx == 0 && dyPx == 0) return;

    // Convert pixel delta → lat/lon delta. See plan for derivation.
    //   delta_lon =       -dx_px * 360 / (256 * 2^z)
    //   delta_lat = +dy_px * 360 * cos(centerLatRad) / (256 * 2^z)
    const double s = 360.0 / (256.0 * (double)(1 << self->_zoom));
    const double cosLat = cos(self->_centerLat * M_PI / 180.0);
    self->_centerLon += -(double)dxPx * s;
    self->_centerLat += (double)dyPx * s * cosLat;

    // Clamp lat to Mercator-safe range (slippy.h already clamps inside
    // latLonToTileXY but keeping the state value sane avoids weird Center
    // behavior near the poles).
    if (self->_centerLat >  SLIPPY_LAT_MAX) self->_centerLat =  SLIPPY_LAT_MAX;
    if (self->_centerLat < -SLIPPY_LAT_MAX) self->_centerLat = -SLIPPY_LAT_MAX;
    while (self->_centerLon >= 180.0) self->_centerLon -= 360.0;
    while (self->_centerLon < -180.0) self->_centerLon += 360.0;

    self->_panLast = now;

    // Throttle redraw — PNG decode + canvas blit at 410×502 can't keep up
    // with the 30 Hz indev polling on a fast swipe.
    if (millis() - self->_lastRenderMs >= 30) {
        self->render();  // updates _lastRenderMs
    }
}

void MapScreen::panReleasedCb(lv_event_t* e) {
    MapScreen* self = static_cast<MapScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_panActive) return;
    self->_panActive = false;

    // Tap vs pan: if the press never crossed the slop dead-zone it's a tap. In
    // general mode, a tap selects the nearest heard marker (name → _selLabel);
    // a pan clears it.
    if (self->_general) {
        if (!self->_panMoved) {
            lv_point_t up = self->_panStart;
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) lv_indev_get_point(indev, &up);
            self->hitTestMarker(up);
        } else {
            self->_hasSel = false;                                 // panned → clear selection
            if (self->_selLabel) lv_obj_add_flag(self->_selLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Commit final position even if throttled away.
    self->render();
}

}  // namespace mclite
