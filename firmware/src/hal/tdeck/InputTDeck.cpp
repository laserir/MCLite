#include "hal/tdeck/InputTDeck.h"

#include "hal/Display.h"
#include "input/Keyboard.h"
#include "input/Trackball.h"
#include "input/Touch.h"

namespace mclite {

void InputTDeck::init() {
    Display::instance().setBootStatus("Keyboard...");
    Keyboard::instance().init();

    Display::instance().setBootStatus("Trackball...");
    Trackball::instance().init();

    Display::instance().setBootStatus("Touchscreen...");
    Touch::instance().init();
}

char InputTDeck::pollKey() {
    return Keyboard::instance().lastKey();
}

void InputTDeck::clearKey() {
    Keyboard::instance().clearKey();
}

void InputTDeck::updatePress() {
    Trackball::instance().updatePress();
}

bool InputTDeck::isPressed() {
    return Trackball::instance().isPressed();
}

bool InputTDeck::hasMoved() {
    return Trackball::instance().hasMoved();
}

uint32_t InputTDeck::holdDurationMs() {
    return Trackball::instance().holdDurationMs();
}

uint32_t InputTDeck::lastHoldDuration() {
    return Trackball::instance().lastHoldDuration();
}

bool InputTDeck::isTouched() {
    return Touch::instance().isTouched();
}

void InputTDeck::setBacklight(uint8_t level) {
    Keyboard::instance().setBacklight(level);
}

bool InputTDeck::has(InputCapability /*cap*/) const {
    return true;
}

void InputTDeck::attachToGroup(lv_group_t* group) {
    _group = group;
    if (auto* d = Keyboard::instance().indev())  lv_indev_set_group(d, group);
    if (auto* d = Trackball::instance().indev()) lv_indev_set_group(d, group);
}

IInput& IInput::instance() {
    static InputTDeck inst;
    return inst;
}

}  // namespace mclite
