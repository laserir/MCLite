#pragma once

#include <lvgl.h>
#include <cstdint>

namespace mclite {

class Trackball {
public:
    bool init();

    lv_indev_t* indev() { return _indev; }

    // Long-press tracking (call from main loop)
    void updatePress();  // Poll click pin state
    bool isPressed() const { return _pressing; }
    uint32_t holdDurationMs() const;

    static Trackball& instance();

private:
    Trackball() = default;
    lv_indev_t* _indev = nullptr;
    lv_indev_drv_t _drv;

    // Long-press tracking
    uint32_t _pressStartMs = 0;
    bool     _pressing     = false;
    bool     _seenRelease  = false;  // GPIO 0 boot guard: ignore until first release

    // Movement accumulators (set by ISR)
    static volatile int16_t _dx;
    static volatile int16_t _dy;
    static volatile bool   _moved;

    static void readCb(lv_indev_drv_t* drv, lv_indev_data_t* data);

public:
    // Returns true if trackball was moved since last call (clears flag)
    bool hasMoved();

    // ISR handlers
    static void IRAM_ATTR isrUp();
    static void IRAM_ATTR isrDown();
    static void IRAM_ATTR isrLeft();
    static void IRAM_ATTR isrRight();
};

}  // namespace mclite
