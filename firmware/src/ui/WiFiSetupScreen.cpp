#include "WiFiSetupScreen.h"

#include "theme.h"
#include "UIManager.h"
#include "../hal/Display.h"
#include "../hal/IInput.h"
#include "../config/ConfigManager.h"
#include "../config/defaults.h"
#include "../i18n/I18n.h"
#include "../ota/UpdateChecker.h"
#include "../util/version.h"
#include "../companion/CompanionService.h"

namespace mclite {

void WiFiSetupScreen::create(lv_obj_t* parent) {
    _screen = lv_win_create(parent, theme::CHAT_HEADER_HEIGHT);
    lv_obj_set_size(_screen, Display::width(),
                    Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT);
    lv_obj_align(_screen, LV_ALIGN_BOTTOM_MID, 0, -theme::FOOTER_HEIGHT);
    lv_obj_set_style_bg_color(_screen, theme::BG_PRIMARY, 0);
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
    lv_obj_set_style_bg_color(header, theme::BG_STATUS_BAR, 0);
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
    lv_obj_set_style_text_color(backLbl, theme::ACCENT, 0);

    // Title
    lv_obj_t* title = lv_win_add_title(_screen, t("wifi_setup_title"));
    lv_obj_set_style_text_font(title, FONT_HEADING, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY, 0);

    // Content area
    lv_obj_t* cont = lv_win_get_content(_screen);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_pad_row(cont, theme::PAD_SMALL, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Control row: WiFi switch + status text
    lv_obj_t* ctl = lv_obj_create(cont);
    lv_obj_set_size(ctl, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ctl, theme::BG_SECONDARY, 0);
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
    lv_obj_set_style_text_color(_statusLabel, theme::TEXT_PRIMARY, 0);

    _switch = lv_switch_create(ctl);
    lv_obj_add_event_cb(_switch, switchCb, LV_EVENT_VALUE_CHANGED, this);

    // Companion-mode row (enabled only while WiFi is connected). Turning it on
    // exposes the radio to a phone/PC client over the MeshCore companion protocol.
    _companionRow = lv_obj_create(cont);
    lv_obj_set_size(_companionRow, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(_companionRow, theme::BG_SECONDARY, 0);
    lv_obj_set_style_radius(_companionRow, 4, 0);
    lv_obj_set_style_border_width(_companionRow, 0, 0);
    lv_obj_set_style_pad_all(_companionRow, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(_companionRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_companionRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_companionRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _companionLabel = lv_label_create(_companionRow);
    lv_label_set_long_mode(_companionLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(_companionLabel, 1);
    lv_obj_set_style_text_font(_companionLabel, FONT_BODY, 0);
    lv_obj_set_style_text_color(_companionLabel, theme::TEXT_PRIMARY, 0);
    lv_label_set_text(_companionLabel, t("wifi_companion"));

    _companionSwitch = lv_switch_create(_companionRow);
    lv_obj_add_event_cb(_companionSwitch, companionSwitchCb, LV_EVENT_VALUE_CHANGED, this);

    // "Check for updates" button (shown only while connected)
    _checkBtn = lv_btn_create(cont);
    lv_obj_set_width(_checkBtn, theme::CONTENT_WIDTH);
    lv_obj_set_style_bg_color(_checkBtn, theme::ACCENT, 0);
    lv_obj_add_event_cb(_checkBtn, checkBtnCb, LV_EVENT_CLICKED, this);
    lv_obj_t* cbl = lv_label_create(_checkBtn);
    lv_label_set_text(cbl, t("wifi_check_updates"));
    lv_obj_center(cbl);
    lv_obj_add_flag(_checkBtn, LV_OBJ_FLAG_HIDDEN);

    // Reboot button — shown only when WiFi is locked out by an active BLE stack,
    // giving a one-tap way to reboot into a clean state where WiFi works.
    _rebootBtn = lv_btn_create(cont);
    lv_obj_set_width(_rebootBtn, theme::CONTENT_WIDTH);
    lv_obj_set_style_bg_color(_rebootBtn, theme::ACCENT, 0);
    lv_obj_add_event_cb(_rebootBtn, rebootBtnCb, LV_EVENT_CLICKED, this);
    lv_obj_t* rbl = lv_label_create(_rebootBtn);
    lv_label_set_text(rbl, t("reboot_now"));
    lv_obj_center(rbl);
    lv_obj_add_flag(_rebootBtn, LV_OBJ_FLAG_HIDDEN);

    // Scrollable network list (shown when not connected)
    _list = lv_obj_create(cont);
    lv_obj_set_size(_list, theme::CONTENT_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(_list, 1);
    lv_obj_set_style_bg_opa(_list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_pad_all(_list, 0, 0);
    lv_obj_set_style_pad_row(_list, theme::PAD_SMALL, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);

    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void WiFiSetupScreen::show() {
    if (!_screen) return;
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    _lastConnected = WiFiManager::instance().isConnected();
    updateStatusUi();
    if (!WiFiManager::instance().isConnected()) rebuildList();
    else clearList();
}

void WiFiSetupScreen::tick() {
    if (!_screen || lv_obj_has_flag(_screen, LV_OBJ_FLAG_HIDDEN)) return;
    if (_pwOverlay) return;                       // don't disturb password entry
    bool c = WiFiManager::instance().isConnected();
    if (c != _lastConnected) {                    // connection state changed → refresh
        _lastConnected = c;
        updateStatusUi();
        if (c) clearList();                       // connected → hide the picker
    }
    // Refresh the companion label when a client attaches/detaches.
    bool cc = CompanionService::instance().clientConnected();
    if (cc != _lastCompanionClient) {
        _lastCompanionClient = cc;
        updateStatusUi();
    }
}

void WiFiSetupScreen::hide() {
    if (!_screen) return;
    closePasswordEntry();
    lv_group_t* grp = UIManager::instance().inputGroup();
    if (grp) {
        if (_list) {
            uint32_t cnt = lv_obj_get_child_cnt(_list);
            for (uint32_t i = 0; i < cnt; i++) lv_group_remove_obj(lv_obj_get_child(_list, i));
        }
        lv_group_remove_obj(_backBtn);
        lv_group_remove_obj(_switch);
        lv_group_remove_obj(_companionSwitch);
        lv_group_remove_obj(_checkBtn);
        lv_group_remove_obj(_rebootBtn);
    }
    // Leave WiFi connected if the user turned it on — the status-bar icon reflects it.
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void WiFiSetupScreen::updateStatusUi() {
    auto& wm = WiFiManager::instance();

    // BLE was used this session → WiFi is unavailable until reboot (can't share
    // the radio/RAM with the BLE stack). Show that and disable the controls.
    if (CompanionService::instance().bleInited()) {
        lv_obj_clear_state(_switch, LV_STATE_CHECKED);
        lv_obj_add_state(_switch, LV_STATE_DISABLED);
        lv_obj_clear_state(_companionSwitch, LV_STATE_CHECKED);
        lv_obj_add_state(_companionSwitch, LV_STATE_DISABLED);
        lv_obj_add_flag(_checkBtn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_rebootBtn, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_statusLabel, t("wifi_ble_reboot"));
        lv_group_t* g = UIManager::instance().inputGroup();
        if (g) { lv_group_add_obj(g, _rebootBtn); lv_group_add_obj(g, _backBtn); }
        return;
    }
    lv_obj_add_flag(_rebootBtn, LV_OBJ_FLAG_HIDDEN);

    bool connected = wm.isConnected();
    if (connected) {
        lv_obj_add_state(_switch, LV_STATE_CHECKED);
        static char buf[96];
        snprintf(buf, sizeof(buf), t("wifi_connected"), wm.connectedSsid().c_str());
        String s = String(buf) + "  " + wm.localIp();
        lv_label_set_text(_statusLabel, s.c_str());
        lv_obj_clear_flag(_checkBtn, LV_OBJ_FLAG_HIDDEN);
    } else if (wm.wantsConnection()) {
        lv_obj_add_state(_switch, LV_STATE_CHECKED);   // switch stays on while reconnecting
        lv_label_set_text(_statusLabel, t("wifi_connecting"));
        lv_obj_add_flag(_checkBtn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_state(_switch, LV_STATE_CHECKED);
        lv_label_set_text(_statusLabel, t("wifi_off"));
        lv_obj_add_flag(_checkBtn, LV_OBJ_FLAG_HIDDEN);
    }

    // WiFi companion row — only operable while WiFi is connected.
    auto& comp = CompanionService::instance();
    if (connected) {
        lv_obj_clear_state(_companionSwitch, LV_STATE_DISABLED);
        bool on = comp.wifiCompanionEnabled();
        if (on) lv_obj_add_state(_companionSwitch, LV_STATE_CHECKED);
        else    lv_obj_clear_state(_companionSwitch, LV_STATE_CHECKED);
        if (on) {
            static char cbuf[96];
            snprintf(cbuf, sizeof(cbuf), t("wifi_companion_addr"), wm.localIp().c_str());
            String cs = cbuf;
            if (comp.clientConnected()) cs += String(" (") + t("wifi_companion_client") + ")";
            lv_label_set_text(_companionLabel, cs.c_str());
        } else {
            lv_label_set_text(_companionLabel, t("wifi_companion"));
        }
    } else {
        // WiFi off → companion can't run: force off + disable the switch.
        if (comp.wifiCompanionEnabled()) comp.setWifiCompanionEnabled(false);
        lv_obj_clear_state(_companionSwitch, LV_STATE_CHECKED);
        lv_obj_add_state(_companionSwitch, LV_STATE_DISABLED);
        lv_label_set_text(_companionLabel, t("wifi_companion"));
    }

    lv_group_t* grp = UIManager::instance().inputGroup();
    if (grp) {
        lv_group_add_obj(grp, _switch);
        if (connected) {
            lv_group_add_obj(grp, _companionSwitch);
            lv_group_add_obj(grp, _checkBtn);
        }
        lv_group_add_obj(grp, _backBtn);
    }
}

void WiFiSetupScreen::clearList() {
    lv_group_t* grp = UIManager::instance().inputGroup();
    if (grp && _list) {
        uint32_t cnt = lv_obj_get_child_cnt(_list);
        for (uint32_t i = 0; i < cnt; i++) lv_group_remove_obj(lv_obj_get_child(_list, i));
    }
    lv_obj_clean(_list);
    _netCount = 0;
}

void WiFiSetupScreen::rebuildList() {
    clearList();

    // Never scan while the BLE stack is up — WiFi.scanNetworks() brings the WiFi
    // driver online and crashes alongside BLE (shared radio/RAM).
    if (CompanionService::instance().bleInited()) {
        updateStatusUi();   // shows the "Reboot to use WiFi" state
        return;
    }

    lv_label_set_text(_statusLabel, t("wifi_scanning"));
    lv_refr_now(NULL);

    _netCount = WiFiManager::instance().scan(_nets, MAX_NETS);
    updateStatusUi();   // restore "Off"/connected text after the scan

    lv_group_t* grp = UIManager::instance().inputGroup();
    for (int i = 0; i < _netCount; i++) {
        lv_obj_t* row = lv_obj_create(_list);
        lv_obj_set_size(row, theme::CONTENT_WIDTH - theme::PAD_SMALL, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row, theme::ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void*)(intptr_t)i);
        lv_obj_add_event_cb(row, rowClickCb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(row, [](lv_event_t* ev) {
            lv_obj_scroll_to_view(lv_event_get_target(ev), LV_ANIM_ON);
        }, LV_EVENT_FOCUSED, nullptr);

        lv_obj_t* name = lv_label_create(row);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_font(name, FONT_BODY, 0);
        lv_obj_set_style_text_color(name, theme::TEXT_PRIMARY, 0);
        lv_label_set_text(name, _nets[i].ssid.c_str());

        lv_obj_t* meta = lv_label_create(row);
        lv_obj_set_style_text_font(meta, FONT_SMALL, 0);
        const String& savedSsid = ConfigManager::instance().config().wifi.ssid;
        bool saved = savedSsid.length() > 0 && _nets[i].ssid == savedSsid;
        lv_obj_set_style_text_color(meta, saved ? theme::ACCENT : theme::TEXT_SECONDARY, 0);
        String m;
        if (saved) m += LV_SYMBOL_OK " ";   // credentials already saved for this network
        m += LV_SYMBOL_WIFI;
        if (!_nets[i].open) m += " *";
        lv_label_set_text(meta, m.c_str());

        if (grp) lv_group_add_obj(grp, row);
    }
    if (grp && _netCount > 0) lv_group_focus_obj(lv_obj_get_child(_list, 0));
}

void WiFiSetupScreen::openPasswordEntry(const String& ssid) {
    _selSsid = ssid;
    closePasswordEntry();

    _pwOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_pwOverlay, Display::width(), Display::height());
    lv_obj_set_pos(_pwOverlay, 0, 0);
    lv_obj_set_style_bg_color(_pwOverlay, theme::BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(_pwOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_pwOverlay, 0, 0);
    lv_obj_clear_flag(_pwOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(_pwOverlay);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, theme::CONTENT_WIDTH);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY, 0);
    lv_label_set_text(lbl, ssid.c_str());
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    _pwTextarea = lv_textarea_create(_pwOverlay);
    lv_textarea_set_one_line(_pwTextarea, true);
    lv_textarea_set_password_mode(_pwTextarea, true);
    lv_textarea_set_max_length(_pwTextarea, 63);
    lv_textarea_set_placeholder_text(_pwTextarea, t("wifi_password"));
    lv_obj_set_width(_pwTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(_pwTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(_pwTextarea, theme::ACCENT, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(_pwTextarea, pwReadyCb, LV_EVENT_READY, this);

    // Cancel button (always reachable — not just Esc/keyboard-cancel)
    lv_obj_t* cancel = lv_btn_create(_pwOverlay);
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY, 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT, LV_STATE_FOCUSED);
    lv_obj_align(cancel, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* self = static_cast<WiFiSetupScreen*>(lv_event_get_user_data(ev));
        if (self) lv_async_call(closePwAsyncCb, self);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* cxl = lv_label_create(cancel);
    lv_label_set_text(cxl, t("btn_cancel"));
    lv_obj_center(cxl);

    _pwGroup = lv_group_create();
    lv_group_add_obj(_pwGroup, _pwTextarea);
    lv_group_add_obj(_pwGroup, cancel);
    lv_group_focus_obj(_pwTextarea);
    IInput::instance().attachToGroup(_pwGroup);

#ifdef PLATFORM_TWATCH
    _pwKbd = lv_keyboard_create(_pwOverlay);
    lv_keyboard_set_textarea(_pwKbd, _pwTextarea);
    // popovers MUST be set before NO_REPEAT — set_popovers rebuilds the ctrl_map
    // from the LVGL default, wiping any flags set beforehand (see ChatScreen).
    lv_keyboard_set_popovers(_pwKbd, true);
    // Disable long-press auto-repeat — otherwise holding a key types it every
    // ~100 ms (LVGL's LONG_PRESSED_REPEAT cycle).
    lv_btnmatrix_set_btn_ctrl_all(_pwKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(_pwKbd, pwReadyCb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(_pwKbd, [](lv_event_t* ev) {
        auto* self = static_cast<WiFiSetupScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        // Mode switches (abc/123/symbols) rebuild the ctrl_map → re-apply
        // NO_REPEAT on each key press. Only on VALUE_CHANGED (not every event)
        // to avoid invalidating all keys per frame.
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_pwKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call(closePwAsyncCb, self);
        }
    }, LV_EVENT_ALL, this);
#endif
}

void WiFiSetupScreen::closePasswordEntry() {
    if (_pwGroup) {
        IInput::instance().attachToGroup(UIManager::instance().inputGroup());
        lv_group_del(_pwGroup);
        _pwGroup = nullptr;
    }
#ifdef PLATFORM_TWATCH
    _pwKbd = nullptr;
#endif
    _pwTextarea = nullptr;
    if (_pwOverlay) { lv_obj_del(_pwOverlay); _pwOverlay = nullptr; }
}

void WiFiSetupScreen::doConnect(const String& ssid, const String& password) {
    // Safety net: never bring WiFi up while the BLE stack is initialized (RAM).
    if (CompanionService::instance().bleInited()) {
        UIManager::instance().showToast(t("wifi_ble_reboot"));
        updateStatusUi();
        return;
    }
    lv_label_set_text(_statusLabel, t("wifi_connecting"));
    lv_refr_now(NULL);

    bool ok = WiFiManager::instance().connect(ssid, password);
    if (!ok) {
        WiFiManager::instance().disconnect();
        int st = WiFiManager::instance().lastStatus();  // 1 == WL_NO_SSID_AVAIL
        updateStatusUi();
        if (_selFromSaved && st != 1) {
            // Stored password no longer works (e.g. the AP password changed).
            // Ask for it again — a successful entry overwrites the saved one.
            // (_selFromSaved is false on the retry, so a second failure falls
            // through to the list instead of trapping the user in the prompt.)
            UIManager::instance().showToast(t("wifi_connect_failed"));
            openPasswordEntry(ssid);
        } else {
            UIManager::instance().showToast(st == 1 ? t("wifi_ssid_not_found")
                                                    : t("wifi_connect_failed"));
            rebuildList();   // back to the picker so they can retry
        }
        return;
    }

    // Connected — persist the working credentials and STAY connected, with the
    // ESP32 stack auto-reconnecting in the background if the link drops.
    auto& cfg = ConfigManager::instance().config();
    cfg.wifi.ssid = ssid;
    cfg.wifi.password = password;
    ConfigManager::instance().save();
    WiFiManager::instance().setPersistent(true);
    _lastConnected = true;

    clearList();          // hide the picker; show connected status + check button
    updateStatusUi();
}

void WiFiSetupScreen::doDisconnect() {
    WiFiManager::instance().disconnect();
    updateStatusUi();
    rebuildList();
}

void WiFiSetupScreen::checkUpdatesNow() {
    if (!WiFiManager::instance().isConnected()) return;

    // Show progress on the button (the check is a blocking HTTPS call that freezes
    // the UI for ~1-2 s, so paint the disabled "Checking…" state first).
    lv_obj_t* lbl = lv_obj_get_child(_checkBtn, 0);
    lv_obj_add_state(_checkBtn, LV_STATE_DISABLED);
    if (lbl) lv_label_set_text(lbl, t("wifi_checking"));
    lv_refr_now(NULL);

    RemoteRelease rel;
    bool newer = UpdateChecker::checkLatest(rel) &&
                 compareVersions(rel.version.c_str(), MCLITE_VERSION) > 0;

    lv_obj_clear_state(_checkBtn, LV_STATE_DISABLED);
    if (lbl) lv_label_set_text(lbl, t("wifi_check_updates"));

    if (newer) UIManager::instance().showWiFiInstallModal(rel.version, rel.url);
    else       UIManager::instance().showToast(t("wifi_no_update"));
}

void WiFiSetupScreen::backBtnCb(lv_event_t* e) {
    UIManager::instance().goHome();
}

void WiFiSetupScreen::switchCb(lv_event_t* e) {
    auto* self = static_cast<WiFiSetupScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    bool on = lv_obj_has_state(self->_switch, LV_STATE_CHECKED);
    // BLE and WiFi can't coexist; once BLE inited, WiFi needs a reboot.
    if (on && CompanionService::instance().bleInited()) {
        lv_obj_clear_state(self->_switch, LV_STATE_CHECKED);
        UIManager::instance().showToast(t("wifi_ble_reboot"));
        return;
    }
    if (on) {
        const auto& cfg = ConfigManager::instance().config();
        if (cfg.wifi.ssid.length() > 0) {
            self->_selSsid = cfg.wifi.ssid;
            self->_selPassword = cfg.wifi.password;
            self->_selFromSaved = true;
            lv_async_call(connectAsyncCb, self);   // connect to saved network
        } else {
            // No saved network — revert the switch and let them pick from the list.
            lv_obj_clear_state(self->_switch, LV_STATE_CHECKED);
            UIManager::instance().showToast(t("wifi_scan_empty"));
        }
    } else {
        self->doDisconnect();
    }
}

void WiFiSetupScreen::companionSwitchCb(lv_event_t* e) {
    auto* self = static_cast<WiFiSetupScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    // Only meaningful while WiFi is up (the switch is disabled otherwise).
    if (!WiFiManager::instance().isConnected()) {
        lv_obj_clear_state(self->_companionSwitch, LV_STATE_CHECKED);
        return;
    }
    bool on = lv_obj_has_state(self->_companionSwitch, LV_STATE_CHECKED);
    CompanionService::instance().setWifiCompanionEnabled(on);  // main loop starts/stops it
    self->updateStatusUi();
}

void WiFiSetupScreen::checkBtnCb(lv_event_t* e) {
    auto* self = static_cast<WiFiSetupScreen*>(lv_event_get_user_data(e));
    if (self) lv_async_call(checkAsyncCb, self);
}

void WiFiSetupScreen::rebootBtnCb(lv_event_t* e) {
    ESP.restart();   // clean boot frees the BLE stack so WiFi can run again
}

void WiFiSetupScreen::rowClickCb(lv_event_t* e) {
    auto* self = static_cast<WiFiSetupScreen*>(lv_event_get_user_data(e));
    lv_obj_t* row = lv_event_get_target(e);
    if (!self) return;
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx < 0 || idx >= self->_netCount) return;
    const auto& cfg = ConfigManager::instance().config();
    if (self->_nets[idx].open) {
        self->_selSsid = self->_nets[idx].ssid;
        self->_selPassword = "";
        self->_selFromSaved = false;
        lv_async_call(connectAsyncCb, self);
    } else if (self->_nets[idx].ssid == cfg.wifi.ssid && cfg.wifi.password.length() > 0) {
        // Saved network — reuse the stored password, no prompt. If it no longer
        // works (AP password changed), doConnect re-opens the password entry.
        self->_selSsid = self->_nets[idx].ssid;
        self->_selPassword = cfg.wifi.password;
        self->_selFromSaved = true;
        lv_async_call(connectAsyncCb, self);
    } else {
        self->openPasswordEntry(self->_nets[idx].ssid);
    }
}

void WiFiSetupScreen::pwReadyCb(lv_event_t* e) {
    auto* self = static_cast<WiFiSetupScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_pwTextarea) return;
    self->_selPassword = String(lv_textarea_get_text(self->_pwTextarea));
    self->_selFromSaved = false;   // freshly typed — a failure shouldn't loop the prompt
    lv_async_call(connectAsyncCb, self);
}

void WiFiSetupScreen::connectAsyncCb(void* user) {
    auto* self = static_cast<WiFiSetupScreen*>(user);
    if (!self) return;
    self->closePasswordEntry();
    self->doConnect(self->_selSsid, self->_selPassword);
}

void WiFiSetupScreen::closePwAsyncCb(void* user) {
    auto* self = static_cast<WiFiSetupScreen*>(user);
    if (self) self->closePasswordEntry();
}

void WiFiSetupScreen::checkAsyncCb(void* user) {
    auto* self = static_cast<WiFiSetupScreen*>(user);
    if (self) self->checkUpdatesNow();
}

}  // namespace mclite
