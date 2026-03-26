#include "Touch.h"
#include "../hal/Display.h"
#include <Wire.h>
#include <Arduino.h>

namespace mclite {

// GT911 register addresses
static constexpr uint16_t GT911_REG_DATA = 0x814E;

Touch& Touch::instance() {
    static Touch inst;
    return inst;
}

static bool gt911_write_reg(uint8_t addr, uint16_t reg, const uint8_t* data, uint8_t len) {
    Wire1.beginTransmission(addr);
    Wire1.write((uint8_t)(reg >> 8));
    Wire1.write((uint8_t)(reg & 0xFF));
    if (data && len) Wire1.write(data, len);
    return Wire1.endTransmission() == 0;
}

// Read status + first touch point in one I2C transaction (9 bytes from 0x814E)
// Layout: [status] [trackId, xLo, xHi, yLo, yHi, sizeLo, sizeHi, reserved]
static bool gt911_read_touch(uint8_t addr, uint8_t* buf9) {
    Wire1.beginTransmission(addr);
    Wire1.write((uint8_t)(GT911_REG_DATA >> 8));
    Wire1.write((uint8_t)(GT911_REG_DATA & 0xFF));
    if (Wire1.endTransmission(false) != 0) return false;
    uint8_t got = Wire1.requestFrom(addr, (uint8_t)9);
    if (got < 9) return false;
    for (uint8_t i = 0; i < 9; i++) {
        buf9[i] = Wire1.read();
    }
    return true;
}

bool Touch::init() {
    // INT pin: drive low briefly to set GT911 address to 0x14
    pinMode(TDECK_TOUCH_INT, OUTPUT);
    digitalWrite(TDECK_TOUCH_INT, LOW);
    delay(10);
    pinMode(TDECK_TOUCH_INT, INPUT);
    delay(50);

    // Touch shares I2C bus with keyboard (Wire1, SDA=18, SCL=8)
    // Wire1.begin() already called by Keyboard::init()
    delay(10);

    // Probe both addresses
    _i2cAddr = 0;
    Wire1.beginTransmission(0x14);
    if (Wire1.endTransmission() == 0) {
        _i2cAddr = 0x14;
    } else {
        Wire1.beginTransmission(0x5D);
        if (Wire1.endTransmission() == 0) {
            _i2cAddr = 0x5D;
        }
    }

    if (_i2cAddr == 0) {
        Serial.println("[Touch] GT911 not found on I2C");
        _available = false;
    } else {
        Serial.printf("[Touch] GT911 found at 0x%02X\n", _i2cAddr);
        _available = true;

        // Clear any pending data
        uint8_t zero = 0;
        gt911_write_reg(_i2cAddr, GT911_REG_DATA, &zero, 1);
    }

    // Register LVGL pointer input device
    lv_indev_drv_init(&_drv);
    _drv.type    = LV_INDEV_TYPE_POINTER;
    _drv.read_cb = readCb;
    _drv.user_data = this;
    _indev = lv_indev_drv_register(&_drv);

    return _available;
}

void Touch::readCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    Touch* self = (Touch*)drv->user_data;

    if (!self->_available) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // Read status + first touch point in single I2C transaction
    uint8_t buf[9];
    if (!gt911_read_touch(self->_i2cAddr, buf)) {
        data->state = LV_INDEV_STATE_RELEASED;
        self->_touched = false;
        return;
    }

    uint8_t status  = buf[0];
    uint8_t touches = status & 0x0F;
    bool dataReady  = (status & 0x80) != 0;

    if (dataReady && touches > 0 && touches <= 5) {
        // buf[1]=trackId, buf[2]=xLo, buf[3]=xHi, buf[4]=yLo, buf[5]=yHi
        uint16_t rawX = buf[2] | ((uint16_t)buf[3] << 8);
        uint16_t rawY = buf[4] | ((uint16_t)buf[5] << 8);

        // GT911 configured for 240x320 portrait (matching panel native)
        // Display uses setRotation(1) for landscape (320x240)
        // Rotation 1: screen_x = rawY, screen_y = (panel_width - 1 - rawX)
        int16_t sx = rawY;
        int16_t sy = 239 - rawX;

        // Clamp
        if (sx < 0) sx = 0;
        if (sx > 319) sx = 319;
        if (sy < 0) sy = 0;
        if (sy > 239) sy = 239;

        data->point.x = sx;
        data->point.y = sy;
        data->state = LV_INDEV_STATE_PRESSED;
        self->_touched = true;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        self->_touched = false;
    }

    // Clear status register for next reading
    if (dataReady) {
        uint8_t zero = 0;
        gt911_write_reg(self->_i2cAddr, GT911_REG_DATA, &zero, 1);
    }
}

}  // namespace mclite
