#include "Keyboard.h"
#include <Wire.h>
#include <Arduino.h>

namespace mclite {

Keyboard& Keyboard::instance() {
    static Keyboard inst;
    return inst;
}

bool Keyboard::init() {
    Wire1.begin(TDECK_KB_SDA, TDECK_KB_SCL, 400000);

    lv_indev_drv_init(&_drv);
    _drv.type = LV_INDEV_TYPE_KEYPAD;
    _drv.read_cb = readCb;
    _drv.user_data = this;
    _indev = lv_indev_drv_register(&_drv);

    Serial.println("[KB] Initialized");
    return true;
}

char Keyboard::readI2C() {
    Wire1.requestFrom((uint8_t)TDECK_KB_I2C_ADDR, (uint8_t)1);
    if (Wire1.available()) {
        char c = Wire1.read();
        if (c != 0) return c;
    }
    return 0;
}

void Keyboard::readCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    Keyboard* self = (Keyboard*)drv->user_data;
    char c = self->readI2C();

    if (c != 0) {
        // Store for shortcut handler
        self->_lastKey = c;

        data->state = LV_INDEV_STATE_PRESSED;

        // Map special keys to LVGL key codes
        switch (c) {
            case '\n':
            case '\r':
                data->key = LV_KEY_ENTER;
                break;
            case '\b':  // 0x08
                data->key = LV_KEY_BACKSPACE;
                break;
            case 0x1B:  // ESC
                data->key = LV_KEY_ESC;
                break;
            case 0xB5:  // T-Deck UP
                data->key = LV_KEY_UP;
                break;
            case 0xB6:  // T-Deck DOWN
                data->key = LV_KEY_DOWN;
                break;
            case 0xB4:  // T-Deck LEFT
                data->key = LV_KEY_LEFT;
                break;
            case 0xB7:  // T-Deck RIGHT
                data->key = LV_KEY_RIGHT;
                break;
            default:
                data->key = c;
                break;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

}  // namespace mclite
