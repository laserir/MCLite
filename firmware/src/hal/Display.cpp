#include "Display.h"
#include "../config/defaults.h"
#include "../ui/theme.h"
#include <Arduino.h>

namespace mclite {

lv_disp_draw_buf_t Display::_drawBuf;
lv_disp_drv_t      Display::_dispDrv;
lv_color_t*        Display::_buf1 = nullptr;
lv_color_t*        Display::_buf2 = nullptr;

// LovyanGFX config for T-Deck Plus
TDeckDisplay::TDeckDisplay() {
    auto busConfig = _bus.config();
    busConfig.spi_host   = SPI2_HOST;
    busConfig.spi_mode   = 0;
    busConfig.freq_write = 40000000;
    busConfig.freq_read  = 16000000;
    busConfig.pin_mosi   = TDECK_SPI_MOSI;
    busConfig.pin_miso   = TDECK_SPI_MISO;
    busConfig.pin_sclk   = TDECK_SPI_SCK;
    busConfig.pin_dc     = TDECK_TFT_DC;
    _bus.config(busConfig);
    _panel.setBus(&_bus);

    auto panelConfig = _panel.config();
    panelConfig.pin_cs       = TDECK_TFT_CS;
    panelConfig.pin_rst      = TDECK_TFT_RST;
    panelConfig.panel_width  = 240;   // Native portrait width
    panelConfig.panel_height = 320;   // Native portrait height
    panelConfig.offset_x     = 0;
    panelConfig.offset_y     = 0;
    panelConfig.invert       = true;
    panelConfig.rgb_order    = false;
    _panel.config(panelConfig);
    setPanel(&_panel);

    auto lightConfig = _light.config();
    lightConfig.pin_bl   = TDECK_TFT_BL;
    lightConfig.invert   = false;
    lightConfig.freq     = 12000;
    lightConfig.pwm_channel = 0;
    _light.config(lightConfig);
    _panel.setLight(&_light);
}

Display& Display::instance() {
    static Display inst;
    return inst;
}

bool Display::init() {
    _lgfx.init();
    _lgfx.setRotation(1);  // Landscape
    setBrightness(180);

    // Initialize LVGL
    lv_init();

    // Allocate draw buffers in PSRAM (double-buffered)
    const size_t bufSize = 320 * 40;  // 40 rows at a time
    _buf1 = (lv_color_t*)ps_malloc(bufSize * sizeof(lv_color_t));
    _buf2 = (lv_color_t*)ps_malloc(bufSize * sizeof(lv_color_t));
    if (!_buf1 || !_buf2) {
        Serial.println("[Display] PSRAM alloc failed, falling back to RAM");
        free(_buf1);  // avoid leak if only one succeeded
        _buf1 = (lv_color_t*)malloc(bufSize * sizeof(lv_color_t));
        _buf2 = nullptr;
    }
    if (!_buf1) {
        Serial.println("[Display] Draw buffer allocation failed");
        return false;
    }

    lv_disp_draw_buf_init(&_drawBuf, _buf1, _buf2, bufSize);

    // Register display driver
    lv_disp_drv_init(&_dispDrv);
    _dispDrv.hor_res  = 320;
    _dispDrv.ver_res  = 240;
    _dispDrv.flush_cb = flushCb;
    _dispDrv.draw_buf = &_drawBuf;
    _dispDrv.user_data = this;
    lv_disp_drv_register(&_dispDrv);

    return true;
}

void Display::setBrightness(uint8_t level) {
    _lgfx.setBrightness(level);
}

void Display::flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* buf) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    _lgfx.startWrite();
    _lgfx.setAddrWindow(area->x1, area->y1, w, h);
    _lgfx.writePixels((uint16_t*)buf, w * h);
    _lgfx.endWrite();
    lv_disp_flush_ready(drv);
}

void Display::flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* buf) {
    Display* self = (Display*)drv->user_data;
    self->flush(drv, area, buf);
}

void Display::showBootScreen(const char* bootText) {
    // Create a dedicated boot screen
    _bootScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_bootScreen, theme::BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(_bootScreen, LV_OPA_COVER, 0);

    // Container for centered content
    lv_obj_t* container = lv_obj_create(_bootScreen);
    lv_obj_set_size(container, 320, 240);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_center(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // "MCLite" title
    lv_obj_t* title = lv_label_create(container);
    lv_label_set_text(title, "MCLite");
    lv_obj_set_style_text_font(title, FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY, 0);

    // Version
    lv_obj_t* version = lv_label_create(container);
    lv_label_set_text_fmt(version, "v%s", MCLITE_VERSION);
    lv_obj_set_style_text_font(version, FONT_SMALL, 0);
    lv_obj_set_style_text_color(version, theme::TEXT_SECONDARY, 0);
    lv_obj_set_style_pad_top(version, 4, 0);

    // Boot text label (e.g. team name) — always created, hidden until set
    _bootSubtitle = lv_label_create(container);
    lv_obj_set_style_text_font(_bootSubtitle, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(_bootSubtitle, theme::ACCENT, 0);
    lv_obj_set_style_pad_top(_bootSubtitle, 12, 0);
    if (bootText && bootText[0] != '\0') {
        lv_label_set_text(_bootSubtitle, bootText);
    } else {
        lv_obj_add_flag(_bootSubtitle, LV_OBJ_FLAG_HIDDEN);
    }

    // Status/progress text at bottom
    _bootStatus = lv_label_create(_bootScreen);
    lv_label_set_text(_bootStatus, "Starting...");
    lv_obj_set_style_text_font(_bootStatus, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_bootStatus, theme::TEXT_TIMESTAMP, 0);
    lv_obj_align(_bootStatus, LV_ALIGN_BOTTOM_MID, 0, -16);

    // Load and render
    lv_scr_load(_bootScreen);
    lv_timer_handler();
}

void Display::setBootText(const char* text) {
    if (!_bootSubtitle) return;
    if (text && text[0] != '\0') {
        lv_label_set_text(_bootSubtitle, text);
        lv_obj_clear_flag(_bootSubtitle, LV_OBJ_FLAG_HIDDEN);
        lv_timer_handler();
    }
}

void Display::setBootStatus(const char* status) {
    if (!_bootStatus) return;
    lv_label_set_text(_bootStatus, status);
    lv_timer_handler();
}

void Display::hideBootScreen() {
    if (_bootScreen && _bootScreen != lv_scr_act()) {
        lv_obj_del(_bootScreen);
        _bootScreen = nullptr;
        _bootStatus = nullptr;
        _bootSubtitle = nullptr;
    }
}

}  // namespace mclite
