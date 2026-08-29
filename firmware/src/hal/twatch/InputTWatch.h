#pragma once

#include "hal/IInput.h"

namespace mclite {

class InputTWatch : public IInput {
public:
    void init() override;

    char pollKey() override     { return 0; }
    void clearKey() override    {}

    void updatePress() override;
    bool isPressed() override   { return _pressing; }
    bool hasMoved() override    { return false; }
    uint32_t holdDurationMs() override;
    uint32_t lastHoldDuration() override { return _lastHoldMs; }

    bool isTouched() override;

    void setBacklight(uint8_t /*level*/) override {}

    bool has(InputCapability cap) const override;

    void attachToGroup(lv_group_t* group) override { _group = group; }
    lv_group_t* currentGroup() const override { return _group; }

private:
    lv_group_t* _group = nullptr;
    // GPIO 0 boot-button hold tracking — mirrors Trackball::updatePress logic.
    uint32_t _pressStartMs = 0;
    uint32_t _lastHoldMs   = 0;
    bool     _pressing     = false;
    bool     _seenRelease  = false;
};

}  // namespace mclite
