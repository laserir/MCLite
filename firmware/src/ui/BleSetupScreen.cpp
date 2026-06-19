#include "BleSetupScreen.h"

#include "theme.h"
#include "UIManager.h"
#include "../hal/Display.h"
#include "../hal/IInput.h"
#include "../i18n/I18n.h"
#include "../companion/CompanionService.h"

namespace mclite {

void BleSetupScreen::create(lv_obj_t* parent) {
    _screen = lv_win_create(parent, theme::CHAT_HEADER_HEIGHT);
    lv_obj_set_size(_screen, Display::width(),
                    Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT);
    lv_obj_align(_screen, LV_ALIGN_BOTTOM_MID, 0, -theme::FOOTER_HEIGHT);
    lv_obj_set_style_bg_color(_screen, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_set_style_radius(_screen, 0, 0);
    lv_obj_set_style_pad_all(_screen, 0, 0);
    lv_obj_set_style_pad_row(_screen, theme::PAD_SMALL, 0);

#ifdef PLATFORM_TWATCH
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(_screen, [](lv_event_t*) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_RIGHT) UIManager::instance().goHome();
    }, LV_EVENT_GESTURE, nullptr);
#endif

    // Style the header
    lv_obj_t* header = lv_win_get_header(_screen);
    lv_obj_set_style_bg_color(header, theme::BG_STATUS_BAR(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, theme::PAD_SMALL, 0);
    lv_obj_set_style_pad_hor(header, theme::CHAT_HEADER_PAD_HOR, 0);

    // Back button
    _backBtn = lv_win_add_btn(_screen, LV_SYMBOL_LEFT, theme::BTN_HEADER_BACK_W);
    lv_obj_set_style_bg_opa(_backBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(_backBtn, 0, 0);
    lv_obj_set_style_border_width(_backBtn, 0, 0);
    lv_obj_add_event_cb(_backBtn, backBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* backLbl = lv_obj_get_child(_backBtn, 0);
    lv_obj_set_style_text_font(backLbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(backLbl, theme::ACCENT(), 0);

    // Title
    lv_obj_t* title = lv_win_add_title(_screen, t("ble_companion"));
    lv_obj_set_style_text_font(title, FONT_HEADING, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY(), 0);

    // Content area
    lv_obj_t* cont = lv_win_get_content(_screen);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_pad_row(cont, theme::PAD_SMALL, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Control row: status text + switch
    lv_obj_t* ctl = lv_obj_create(cont);
    lv_obj_set_size(ctl, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ctl, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_radius(ctl, 4, 0);
    lv_obj_set_style_border_width(ctl, 0, 0);
    lv_obj_set_style_pad_all(ctl, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(ctl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ctl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctl, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _statusLabel = lv_label_create(ctl);
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(_statusLabel, 1);
    lv_obj_set_style_text_font(_statusLabel, FONT_BODY, 0);
    lv_obj_set_style_text_color(_statusLabel, theme::TEXT_PRIMARY(), 0);

    _switch = lv_switch_create(ctl);
    lv_obj_set_style_bg_color(_switch, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(_switch, switchCb, LV_EVENT_VALUE_CHANGED, this);

    // Pairing PIN — shown prominently so the phone can enter it.
    _pinLabel = lv_label_create(cont);
    lv_obj_set_width(_pinLabel, theme::CONTENT_WIDTH);
    lv_obj_set_style_pad_all(_pinLabel, theme::PAD_SMALL, 0);
    lv_obj_set_style_text_align(_pinLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(_pinLabel, FONT_TITLE, 0);
    lv_obj_set_style_text_color(_pinLabel, theme::ACCENT(), 0);

    // Hint
    lv_obj_t* hint = lv_label_create(cont);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, theme::CONTENT_WIDTH);
    lv_obj_set_style_pad_all(hint, theme::PAD_SMALL, 0);
    lv_obj_set_style_text_font(hint, FONT_BODY, 0);
    lv_obj_set_style_text_color(hint, theme::TEXT_SECONDARY(), 0);
    lv_label_set_text(hint, t("ble_companion_hint"));

    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void BleSetupScreen::show() {
    if (!_screen) return;
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    _lastClient = CompanionService::instance().clientConnected();
    // Ensure a PIN exists so it can be displayed before the user enables BLE.
    uint32_t pin = CompanionService::instance().ensureBlePin();
    static char pinBuf[40];
    snprintf(pinBuf, sizeof(pinBuf), t("ble_companion_pin"), (unsigned long)pin);
    lv_label_set_text(_pinLabel, pinBuf);
    updateUi();
    lv_group_t* grp = UIManager::instance().inputGroup();
    if (grp) {
        lv_group_add_obj(grp, _switch);
        lv_group_add_obj(grp, _backBtn);
    }
}

void BleSetupScreen::hide() {
    if (!_screen) return;
    lv_group_t* grp = UIManager::instance().inputGroup();
    if (grp) {
        lv_group_remove_obj(_switch);
        lv_group_remove_obj(_backBtn);
    }
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void BleSetupScreen::tick() {
    if (!_screen || lv_obj_has_flag(_screen, LV_OBJ_FLAG_HIDDEN)) return;
    bool c = CompanionService::instance().clientConnected();
    if (c != _lastClient) { _lastClient = c; updateUi(); }
}

void BleSetupScreen::updateUi() {
    auto& comp = CompanionService::instance();
    bool on = comp.bleCompanionEnabled();
    if (on) lv_obj_add_state(_switch, LV_STATE_CHECKED);
    else    lv_obj_clear_state(_switch, LV_STATE_CHECKED);
    if (!on) {
        lv_label_set_text(_statusLabel, t("off"));
    } else if (comp.clientConnected()) {
        lv_label_set_text(_statusLabel, t("wifi_companion_client"));   // "connected"
    } else {
        lv_label_set_text(_statusLabel, t("ble_companion_advertising"));
    }
}

void BleSetupScreen::switchCb(lv_event_t* e) {
    auto* self = static_cast<BleSetupScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    bool on = lv_obj_has_state(self->_switch, LV_STATE_CHECKED);
    CompanionService::instance().setBleCompanionEnabled(on);  // mutually exclusive with WiFi/USB
    self->updateUi();
}

void BleSetupScreen::backBtnCb(lv_event_t* e) {
    UIManager::instance().showScreen(Screen::ADMIN);
}

}  // namespace mclite
