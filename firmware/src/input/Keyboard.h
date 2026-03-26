#pragma once

#include <lvgl.h>
#include <cstdint>

namespace mclite {

class Keyboard {
public:
    bool init();

    // Last key pressed (0 if none). Set by LVGL read callback.
    char lastKey() const { return _lastKey; }
    void clearKey() { _lastKey = 0; }

    // Register with LVGL as input device
    lv_indev_t* indev() { return _indev; }

    static Keyboard& instance();

private:
    Keyboard() = default;
    volatile char _lastKey = 0;
    lv_indev_t* _indev = nullptr;
    lv_indev_drv_t _drv;

    static void readCb(lv_indev_drv_t* drv, lv_indev_data_t* data);
    char readI2C();
};

}  // namespace mclite
