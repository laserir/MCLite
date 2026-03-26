#pragma once

#include <lvgl.h>

namespace mclite {

class Touch {
public:
    bool init();

    lv_indev_t* indev() { return _indev; }

    bool isTouched() const { return _touched; }
    bool isAvailable() const { return _available; }

    static Touch& instance();

private:
    Touch() = default;
    lv_indev_t*    _indev     = nullptr;
    lv_indev_drv_t _drv;
    bool           _touched   = false;
    bool           _available = false;
    uint8_t        _i2cAddr   = 0;
    uint16_t       _resX      = 320;   // GT911 configured X resolution
    uint16_t       _resY      = 240;   // GT911 configured Y resolution

    static void readCb(lv_indev_drv_t* drv, lv_indev_data_t* data);
};

}  // namespace mclite
