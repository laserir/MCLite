#pragma once

#include <lvgl.h>
#include <cstdint>

namespace mclite {

enum class InputCapability {
    Keyboard,
};

class IInput {
public:
    static IInput& instance();

    virtual ~IInput() = default;

    virtual void init() = 0;

    virtual char pollKey() = 0;
    virtual void clearKey() = 0;

    virtual void updatePress() = 0;
    virtual bool isPressed() = 0;
    virtual bool hasMoved() = 0;
    virtual uint32_t holdDurationMs() = 0;
    virtual uint32_t lastHoldDuration() = 0;

    virtual bool isTouched() = 0;

    virtual void setBacklight(uint8_t level) = 0;

    virtual bool has(InputCapability cap) const = 0;

    // Pointer (touch) indevs aren't routed through groups; implementations may
    // ignore the parameter.
    virtual void attachToGroup(lv_group_t* group) = 0;
    // The group most recently passed to attachToGroup(). LVGL has no public
    // getter for an indev's group, and the PIN overlay needs to hand input back
    // to exactly what it interrupted.
    virtual lv_group_t* currentGroup() const = 0;
};

}  // namespace mclite
