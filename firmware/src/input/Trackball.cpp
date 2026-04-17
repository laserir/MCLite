#include "Trackball.h"
#include "../ui/UIManager.h"
#include <Arduino.h>

namespace mclite {

volatile int16_t Trackball::_dx = 0;
volatile int16_t Trackball::_dy = 0;
volatile bool   Trackball::_moved = false;

Trackball& Trackball::instance() {
    static Trackball inst;
    return inst;
}

bool Trackball::init() {
    pinMode(TDECK_TRACKBALL_UP,    INPUT_PULLUP);
    pinMode(TDECK_TRACKBALL_DOWN,  INPUT_PULLUP);
    pinMode(TDECK_TRACKBALL_LEFT,  INPUT_PULLUP);
    pinMode(TDECK_TRACKBALL_RIGHT, INPUT_PULLUP);
    pinMode(TDECK_TRACKBALL_CLICK, INPUT);  // GPIO 0 — no internal pull (has external bias)

    attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_UP),    isrUp,    FALLING);
    attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_DOWN),  isrDown,  FALLING);
    attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_LEFT),  isrLeft,  FALLING);
    attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_RIGHT), isrRight, FALLING);
    // No ISR for click — polled directly via digitalRead in readCb/updatePress

    // Register as LVGL encoder device (up/down/click maps to list navigation)
    lv_indev_drv_init(&_drv);
    _drv.type = LV_INDEV_TYPE_ENCODER;
    _drv.read_cb = readCb;
    _drv.user_data = this;
    _indev = lv_indev_drv_register(&_drv);

    Serial.println("[Trackball] Initialized");
    return true;
}

void Trackball::readCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    Trackball* self = (Trackball*)drv->user_data;

    noInterrupts();
    int16_t dy = _dy;
    _dx = 0;
    _dy = 0;
    interrupts();

    // Key-locked: suppress LVGL encoder events (_moved ISR flag still propagates to checkWake)
    if (UIManager::instance().isKeyLocked()) {
        self->_deliverClick = false;  // Don't leak clicks on unlock
        data->enc_diff = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    data->enc_diff = dy;

    // Deferred click: suppress PRESSED while held, deliver one-shot on short release.
    // updatePress() sets _deliverClick when a press < CLICK_DELAY_MS ends.
    // This prevents long holds (key lock, SOS) from triggering LVGL selection.
    if (self->_deliverClick) {
        data->state = LV_INDEV_STATE_PRESSED;
        self->_deliverClick = false;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

uint32_t Trackball::holdDurationMs() const {
    if (!_pressing) return 0;
    return millis() - _pressStartMs;
}

void Trackball::updatePress() {
    bool pinDown = (digitalRead(TDECK_TRACKBALL_CLICK) == LOW);

    // Boot guard: GPIO 0 is a strapping pin — can be LOW during startup.
    // Don't register any press until we've seen it go HIGH at least once.
    if (!_seenRelease) {
        if (!pinDown) _seenRelease = true;
        return;
    }

    if (pinDown && !_pressing) {
        _pressing = true;
        _pressStartMs = millis();
    } else if (!pinDown && _pressing) {
        _lastHoldMs = millis() - _pressStartMs;
        _pressing = false;
        if (_lastHoldMs < CLICK_DELAY_MS) {
            _deliverClick = true;  // Short press — deliver to LVGL
        }
    }
}

bool Trackball::hasMoved() {
    noInterrupts();
    bool m = _moved;
    _moved = false;
    interrupts();
    return m;
}

void IRAM_ATTR Trackball::isrUp()    { _dy--; _moved = true; }
void IRAM_ATTR Trackball::isrDown()  { _dy++; _moved = true; }
void IRAM_ATTR Trackball::isrLeft()  { _dx--; _moved = true; }
void IRAM_ATTR Trackball::isrRight() { _dx++; _moved = true; }
// isrClick removed — click is polled via digitalRead in readCb/updatePress

}  // namespace mclite
