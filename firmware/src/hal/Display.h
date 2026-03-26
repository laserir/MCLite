#pragma once

#include <LovyanGFX.hpp>
#include <lvgl.h>

namespace mclite {

// LovyanGFX display configuration for T-Deck Plus ST7789
class TDeckDisplay : public lgfx::LGFX_Device {
public:
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;

    TDeckDisplay();
};

class Display {
public:
    bool init();
    void setBrightness(uint8_t level);  // 0-255
    void flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* buf);

    // LVGL boot screen — call after init(), updates with lv_timer_handler()
    void showBootScreen(const char* bootText = nullptr);
    void setBootText(const char* text);      // Set/show boot subtitle after config load
    void setBootStatus(const char* status);  // Update progress text
    void hideBootScreen();                    // Remove boot screen, show normal UI

    TDeckDisplay& getLGFX() { return _lgfx; }

    static Display& instance();

private:
    Display() = default;
    TDeckDisplay _lgfx;
    static lv_disp_draw_buf_t _drawBuf;
    static lv_disp_drv_t      _dispDrv;
    static lv_color_t*        _buf1;
    static lv_color_t*        _buf2;

    // Boot screen LVGL objects
    lv_obj_t* _bootScreen   = nullptr;
    lv_obj_t* _bootSubtitle = nullptr;
    lv_obj_t* _bootStatus   = nullptr;

    static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* buf);
};

}  // namespace mclite
