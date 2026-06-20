#include "MessagingSettingsScreen.h"
#include <Arduino.h>
#include <vector>
#include "UIManager.h"
#include "theme.h"
#include "../config/ConfigManager.h"
#include "../hal/Display.h"
#include "../hal/IInput.h"
#include "../i18n/I18n.h"
#include "../config/defaults.h"

namespace mclite {

namespace {
// File-scope state for picker btnmatrix callbacks
std::vector<String>      g_locFormatNames;
std::vector<String>      g_locFormatLabels;
std::vector<const char*> g_locFormatMap;

std::vector<String>      g_showTelemNames;
std::vector<String>      g_showTelemLabels;
std::vector<const char*> g_showTelemMap;
}  // namespace

void MessagingSettingsScreen::create(lv_obj_t* parent) {
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
    lv_obj_add_event_cb(_screen, [](lv_event_t* e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_RIGHT) UIManager::instance().goHome();
    }, LV_EVENT_GESTURE, nullptr);
#endif

    // Header styling
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
    lv_obj_t* title = lv_win_add_title(_screen, t("messaging_settings_title"));
    lv_obj_set_style_text_font(title, FONT_HEADING, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY(), 0);

    // Content area
    _content = lv_win_get_content(_screen);
    lv_obj_set_style_bg_opa(_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_content, 0, 0);
    lv_obj_set_style_pad_all(_content, theme::PAD_MEDIUM, 0);
    lv_obj_set_style_pad_row(_content, theme::PAD_SMALL, 0);
    lv_obj_set_flex_flow(_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(_content, LV_OBJ_FLAG_SCROLLABLE);

#ifdef PLATFORM_TWATCH
    lv_obj_set_style_pad_hor(_content, theme::SAFE_AREA_LEFT, 0);
    lv_obj_set_style_pad_ver(_content, theme::PAD_MEDIUM, 0);
    lv_obj_set_scroll_dir(_content, LV_DIR_VER);
#endif

    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* MessagingSettingsScreen::createRowContainer(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUSED);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    return row;
}

void MessagingSettingsScreen::show() {
    if (!_screen) return;
    lv_obj_clean(_content);

    const auto& cfg = ConfigManager::instance().config();

    auto addSection = [this](const char* title) {
        lv_obj_t* lbl = lv_label_create(_content);
        lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
        lv_obj_set_style_text_color(lbl, theme::ACCENT(), 0);
        lv_obj_set_style_pad_top(lbl, theme::PAD_MEDIUM, 0);
        lv_label_set_text(lbl, title);
    };

    // --- History ---
    addSection(t("sec_history"));

    // Save History toggle
    {
        lv_obj_t* row = createRowContainer(_content);
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_save_history"));

        lv_obj_t* sw = lv_switch_create(row);
        lv_obj_set_style_bg_color(sw, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (cfg.messaging.saveHistory) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, saveHistoryToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Max History Per Chat editor
    {
        lv_obj_t* row = createRowContainer(_content);
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_width(lbl, LV_PCT(50));
        lv_label_set_text(lbl, t("lbl_max_per_chat"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        String valueText = String(cfg.messaging.maxHistoryPerChat) + "  " LV_SYMBOL_RIGHT;
        lv_label_set_text(val, valueText.c_str());

        lv_obj_add_event_cb(row, historyRowCb, LV_EVENT_CLICKED, this);
    }

    // --- Location ---
    addSection(t("sec_location"));

    // Location Format picker
    {
        lv_obj_t* row = createRowContainer(_content);
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_location_format"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        String fmt = cfg.messaging.locationFormat;
        if (fmt == "decimal") fmt = t("fmt_decimal");
        else if (fmt == "dms") fmt = t("fmt_dms");
        else if (fmt == "mgrs") fmt = t("fmt_mgrs");
        else if (fmt == "utm") fmt = t("fmt_utm");
        lv_label_set_text(val, (fmt + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, locFormatRowCb, LV_EVENT_CLICKED, this);
    }

    // --- Messaging ---
    addSection(t("sec_messaging"));

    // Max Retries slider
    {
        lv_obj_t* row = createRowContainer(_content);
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_obj_set_width(lbl, LV_PCT(50));
        lv_label_set_text(lbl, t("lbl_max_retries"));

        _maxRetriesSlider = lv_slider_create(row);
        lv_obj_set_width(_maxRetriesSlider, LV_PCT(40));
        lv_slider_set_range(_maxRetriesSlider, 1, 5);
        lv_slider_set_value(_maxRetriesSlider, cfg.messaging.maxRetries, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_maxRetriesSlider, theme::ACCENT(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_maxRetriesSlider, theme::ACCENT(), LV_PART_KNOB);
        lv_obj_add_event_cb(_maxRetriesSlider, inlineSliderChangedCb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(_maxRetriesSlider, inlineSliderReleasedCb, LV_EVENT_RELEASED, this);

        _maxRetriesValLbl = lv_label_create(row);
        lv_obj_set_style_text_font(_maxRetriesValLbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(_maxRetriesValLbl, theme::TEXT_PRIMARY(), 0);
        lv_obj_set_width(_maxRetriesValLbl, LV_PCT(10));
        lv_label_set_text(_maxRetriesValLbl, String(cfg.messaging.maxRetries).c_str());
    }

    // Request Telemetry toggle
    {
        lv_obj_t* row = createRowContainer(_content);
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_req_telemetry"));

        lv_obj_t* sw = lv_switch_create(row);
        lv_obj_set_style_bg_color(sw, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (cfg.messaging.requestTelemetry) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, requestTelemetryToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Show Telemetry picker
    {
        lv_obj_t* row = createRowContainer(_content);
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_telemetry_badges"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        String telem = cfg.messaging.showTelemetry;
        if (telem == "battery") telem = t("telem_battery");
        else if (telem == "location") telem = t("telem_location");
        else if (telem == "both") telem = t("telem_both");
        else if (telem == "none") telem = t("telem_none");
        lv_label_set_text(val, (telem + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, showTelemRowCb, LV_EVENT_CLICKED, this);
    }

    // Canned Messages toggle
    {
        lv_obj_t* row = createRowContainer(_content);
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_canned_messages"));

        lv_obj_t* sw = lv_switch_create(row);
        lv_obj_set_style_bg_color(sw, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (cfg.messaging.cannedMessages) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, cannedMessagesToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Allow Mute toggle
    {
        lv_obj_t* row = createRowContainer(_content);
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_allow_mute"));

        lv_obj_t* sw = lv_switch_create(row);
        lv_obj_set_style_bg_color(sw, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (cfg.messaging.allowMute) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, allowMuteToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Auto Telemetry toggle
    {
        lv_obj_t* row = createRowContainer(_content);
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_auto_telemetry"));

        lv_obj_t* sw = lv_switch_create(row);
        lv_obj_set_style_bg_color(sw, theme::ACCENT(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (cfg.messaging.autoTelemetry) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, autoTelemetryToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // --- Canned Custom ---
    addSection(t("sec_canned_custom"));

    for (int i = 0; i < 8; i++) {
        lv_obj_t* row = createRowContainer(_content);
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        char slotLbl[16];
        snprintf(slotLbl, sizeof(slotLbl), "%s %d", t("lbl_canned_slot"), i + 1);
        lv_label_set_text(lbl, slotLbl);

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        String text = (i < (int)cfg.messaging.cannedCustom.size())
                            ? cfg.messaging.cannedCustom[i]
                            : String("");
        if (text.length() == 0) text = t("empty");
        if (text.length() > 20) text = text.substring(0, 20) + "…";
        lv_label_set_text(val, (text + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, cannedCustomRowCb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(row, (void*)(intptr_t)i);
    }

    // Input group
    lv_group_t* grp = lv_group_get_default();
    if (grp) {
        lv_group_remove_obj(_backBtn);
        lv_group_remove_obj(_content);
        lv_group_add_obj(grp, _backBtn);
        lv_group_add_obj(grp, _content);
        lv_group_focus_obj(_content);
        lv_group_set_editing(grp, true);
    }

    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void MessagingSettingsScreen::hide() {
    if (_screen) {
        if (_locFormatBtnm) hideLocFormatPicker();
        if (_showTelemBtnm) hideShowTelemPicker();
        if (_cannedTextarea) hideCannedEditor();
        if (_historyTextarea) hideHistoryEditor();
        lv_group_t* grp = lv_group_get_default();
        if (grp) {
            lv_group_set_editing(grp, false);
            lv_group_remove_obj(_backBtn);
            lv_group_remove_obj(_content);
        }
        lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    }
}

void MessagingSettingsScreen::tick() {
    // No live refresh needed
}

void MessagingSettingsScreen::backBtnCb(lv_event_t* e) {
    UIManager::instance().showScreen(Screen::ADMIN);
}

// ------------------------------------------------------------------
// Toggle callbacks
// ------------------------------------------------------------------

void MessagingSettingsScreen::saveHistoryToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    mgr.config().messaging.saveHistory = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.save();
}

void MessagingSettingsScreen::requestTelemetryToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    mgr.config().messaging.requestTelemetry = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.save();
}

void MessagingSettingsScreen::cannedMessagesToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    mgr.config().messaging.cannedMessages = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.save();
}

void MessagingSettingsScreen::allowMuteToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    mgr.config().messaging.allowMute = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.save();
}

void MessagingSettingsScreen::autoTelemetryToggleCb(lv_event_t* e) {
    auto& mgr = ConfigManager::instance();
    lv_obj_t* sw = lv_event_get_target(e);
    mgr.config().messaging.autoTelemetry = lv_obj_has_state(sw, LV_STATE_CHECKED);
    mgr.save();
}

// ------------------------------------------------------------------
// Inline slider callbacks
// ------------------------------------------------------------------

void MessagingSettingsScreen::inlineSliderChangedCb(lv_event_t* e) {
    MessagingSettingsScreen* self = (MessagingSettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);

    if (slider == self->_maxRetriesSlider) {
        lv_label_set_text(self->_maxRetriesValLbl, String(v).c_str());
    }
}

void MessagingSettingsScreen::inlineSliderReleasedCb(lv_event_t* e) {
    MessagingSettingsScreen* self = (MessagingSettingsScreen*)lv_event_get_user_data(e);
    if (!self) return;
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);

    auto& mgr = ConfigManager::instance();
    if (slider == self->_maxRetriesSlider) {
        mgr.config().messaging.maxRetries = (uint8_t)v;
        mgr.save();
    }
}

// ------------------------------------------------------------------
// History number editor

void MessagingSettingsScreen::historyRowCb(lv_event_t* e) {
    MessagingSettingsScreen* self = (MessagingSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_historyTextarea) return;

    const auto& cfg = ConfigManager::instance().config();
    self->_historyOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_historyOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_historyOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_historyOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(self->_historyOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(self->_historyOverlay, 0, 0);
    lv_obj_clear_flag(self->_historyOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(self->_historyOverlay);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(lbl, t("lbl_max_per_chat"));
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    self->_historyTextarea = lv_textarea_create(self->_historyOverlay);
    lv_textarea_set_one_line(self->_historyTextarea, true);
    lv_textarea_set_max_length(self->_historyTextarea, 4);
    lv_textarea_set_placeholder_text(self->_historyTextarea, t("lbl_max_per_chat"));
    lv_textarea_set_text(self->_historyTextarea, String(cfg.messaging.maxHistoryPerChat).c_str());
    lv_textarea_set_accepted_chars(self->_historyTextarea, "0123456789");
    lv_obj_set_width(self->_historyTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(self->_historyTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(self->_historyTextarea, theme::ACCENT(), LV_STATE_FOCUSED);

    lv_obj_t* btnRow = lv_obj_create(self->_historyOverlay);
    lv_obj_set_size(btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, theme::PAD_MEDIUM, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, historyReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<MessagingSettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((MessagingSettingsScreen*)p)->hideHistoryEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    lv_group_add_obj(g, self->_historyTextarea);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(self->_historyTextarea);
    UIManager::instance().switchToModalGroup(self->_historyOverlay);
    IInput::instance().attachToGroup(g);
    lv_obj_add_event_cb(self->_historyTextarea, historyReadyCb, LV_EVENT_READY, self);

#ifdef PLATFORM_TWATCH
    self->_historyKbd = lv_keyboard_create(self->_historyOverlay);
    lv_keyboard_set_textarea(self->_historyKbd, self->_historyTextarea);
    lv_keyboard_set_popovers(self->_historyKbd, true);
    lv_btnmatrix_set_btn_ctrl_all(self->_historyKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(self->_historyKbd, historyReadyCb, LV_EVENT_READY, self);
    lv_obj_add_event_cb(self->_historyKbd, [](lv_event_t* ev) {
        auto* self = static_cast<MessagingSettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_historyKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((MessagingSettingsScreen*)p)->hideHistoryEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

void MessagingSettingsScreen::historyReadyCb(lv_event_t* e) {
    MessagingSettingsScreen* self = (MessagingSettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_historyTextarea) return;
    const char* text = lv_textarea_get_text(self->_historyTextarea);
    String newText = text ? String(text) : String("");
    // Trim whitespace
    const char* s = newText.c_str();
    int len = strlen(s);
    int l = 0, r = len - 1;
    while (l <= r && isspace((unsigned char)s[l])) ++l;
    while (r >= l && isspace((unsigned char)s[r])) --r;
    if (l > 0 || r < len - 1) {
        newText = newText.substring(l, r + 1);
    }

    int value = newText.length() > 0 ? atoi(newText.c_str()) : 0;
    if (value < 10) value = 10;
    if (value > 500) value = 500;

    auto& mgr = ConfigManager::instance();
    if (mgr.config().messaging.maxHistoryPerChat != (uint16_t)value) {
        mgr.config().messaging.maxHistoryPerChat = (uint16_t)value;
        mgr.save();
    }
    lv_async_call([](void* p) { ((MessagingSettingsScreen*)p)->hideHistoryEditor(); }, self);
}

void MessagingSettingsScreen::hideHistoryEditor() {
    if (!_historyTextarea) return;
    UIManager::instance().restoreFromModalGroup();
#ifdef PLATFORM_TWATCH
    _historyKbd = nullptr;
#endif
    _historyTextarea = nullptr;
    lv_obj_del_async(_historyOverlay);
    _historyOverlay = nullptr;
    if (_screen) show();
}

// ------------------------------------------------------------------
// Location format picker
// ------------------------------------------------------------------

void MessagingSettingsScreen::locFormatRowCb(lv_event_t* e) {
    MessagingSettingsScreen* self = (MessagingSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_locFormatBtnm) return;

    g_locFormatNames.clear(); g_locFormatLabels.clear(); g_locFormatMap.clear();
    g_locFormatNames.push_back("decimal");
    g_locFormatNames.push_back("dms");
    g_locFormatNames.push_back("mgrs");
    g_locFormatNames.push_back("utm");
    g_locFormatLabels.push_back(t("fmt_decimal"));
    g_locFormatLabels.push_back(t("fmt_dms"));
    g_locFormatLabels.push_back(t("fmt_mgrs"));
    g_locFormatLabels.push_back(t("fmt_utm"));
    g_locFormatLabels.push_back(t("btn_cancel"));

    for (size_t i = 0; i < g_locFormatLabels.size(); i++) {
        if (i > 0) g_locFormatMap.push_back("\n");
        g_locFormatMap.push_back(g_locFormatLabels[i].c_str());
    }
    g_locFormatMap.push_back("");

    self->_locFormatBtnm = lv_btnmatrix_create(lv_layer_top());
    lv_btnmatrix_set_map(self->_locFormatBtnm, g_locFormatMap.data());
#ifdef PLATFORM_TWATCH
    lv_coord_t rowH = 64;
#else
    lv_coord_t rowH = 26;
#endif
    lv_coord_t pickerH = (int)g_locFormatLabels.size() * rowH + 8;
    lv_coord_t maxH = Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT - 16;
    if (pickerH > maxH) pickerH = maxH;
    lv_obj_set_size(self->_locFormatBtnm, theme::MODAL_TEXT_WIDTH, pickerH);
    lv_obj_align(self->_locFormatBtnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(self->_locFormatBtnm, FONT_HEADING, 0);
    lv_obj_set_style_bg_color(self->_locFormatBtnm, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(self->_locFormatBtnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(self->_locFormatBtnm, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(self->_locFormatBtnm, 1, 0);
    lv_obj_set_style_radius(self->_locFormatBtnm, 8, 0);
    lv_obj_set_style_bg_color(self->_locFormatBtnm, theme::BG_INPUT(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(self->_locFormatBtnm, theme::TEXT_PRIMARY(), LV_PART_ITEMS);
    lv_obj_set_style_radius(self->_locFormatBtnm, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(self->_locFormatBtnm, theme::ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(self->_locFormatBtnm, theme::TEXT_ON_ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(self->_locFormatBtnm, locFormatChosenCb, LV_EVENT_VALUE_CHANGED, self);
    UIManager::instance().switchToModalGroup(self->_locFormatBtnm);
}

void MessagingSettingsScreen::locFormatChosenCb(lv_event_t* e) {
    MessagingSettingsScreen* self = (MessagingSettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_locFormatBtnm) return;
    uint16_t idx = lv_btnmatrix_get_selected_btn(self->_locFormatBtnm);
    if (idx == LV_BTNMATRIX_BTN_NONE) return;
    if (idx < g_locFormatNames.size()) {
        auto& mgr = ConfigManager::instance();
        String newFmt = g_locFormatNames[idx];
        if (mgr.config().messaging.locationFormat != newFmt) {
            mgr.config().messaging.locationFormat = newFmt;
            mgr.save();
        }
    }
    lv_async_call([](void* p) { ((MessagingSettingsScreen*)p)->hideLocFormatPicker(); }, self);
}

void MessagingSettingsScreen::hideLocFormatPicker() {
    if (!_locFormatBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    lv_obj_del_async(_locFormatBtnm);
    _locFormatBtnm = nullptr;
    if (_screen) show();
}

// ------------------------------------------------------------------
// Show telemetry picker
// ------------------------------------------------------------------

void MessagingSettingsScreen::showTelemRowCb(lv_event_t* e) {
    MessagingSettingsScreen* self = (MessagingSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_showTelemBtnm) return;

    g_showTelemNames.clear(); g_showTelemLabels.clear(); g_showTelemMap.clear();
    g_showTelemNames.push_back("battery");
    g_showTelemNames.push_back("location");
    g_showTelemNames.push_back("both");
    g_showTelemNames.push_back("none");
    g_showTelemLabels.push_back(t("telem_battery"));
    g_showTelemLabels.push_back(t("telem_location"));
    g_showTelemLabels.push_back(t("telem_both"));
    g_showTelemLabels.push_back(t("telem_none"));
    g_showTelemLabels.push_back(t("btn_cancel"));

    for (size_t i = 0; i < g_showTelemLabels.size(); i++) {
        if (i > 0) g_showTelemMap.push_back("\n");
        g_showTelemMap.push_back(g_showTelemLabels[i].c_str());
    }
    g_showTelemMap.push_back("");

    self->_showTelemBtnm = lv_btnmatrix_create(lv_layer_top());
    lv_btnmatrix_set_map(self->_showTelemBtnm, g_showTelemMap.data());
#ifdef PLATFORM_TWATCH
    lv_coord_t rowH = 64;
#else
    lv_coord_t rowH = 26;
#endif
    lv_coord_t pickerH = (int)g_showTelemLabels.size() * rowH + 8;
    lv_coord_t maxH = Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT - 16;
    if (pickerH > maxH) pickerH = maxH;
    lv_obj_set_size(self->_showTelemBtnm, theme::MODAL_TEXT_WIDTH, pickerH);
    lv_obj_align(self->_showTelemBtnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(self->_showTelemBtnm, FONT_HEADING, 0);
    lv_obj_set_style_bg_color(self->_showTelemBtnm, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(self->_showTelemBtnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(self->_showTelemBtnm, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(self->_showTelemBtnm, 1, 0);
    lv_obj_set_style_radius(self->_showTelemBtnm, 8, 0);
    lv_obj_set_style_bg_color(self->_showTelemBtnm, theme::BG_INPUT(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(self->_showTelemBtnm, theme::TEXT_PRIMARY(), LV_PART_ITEMS);
    lv_obj_set_style_radius(self->_showTelemBtnm, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(self->_showTelemBtnm, theme::ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(self->_showTelemBtnm, theme::TEXT_ON_ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(self->_showTelemBtnm, showTelemChosenCb, LV_EVENT_VALUE_CHANGED, self);
    UIManager::instance().switchToModalGroup(self->_showTelemBtnm);
}

void MessagingSettingsScreen::showTelemChosenCb(lv_event_t* e) {
    MessagingSettingsScreen* self = (MessagingSettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_showTelemBtnm) return;
    uint16_t idx = lv_btnmatrix_get_selected_btn(self->_showTelemBtnm);
    if (idx == LV_BTNMATRIX_BTN_NONE) return;
    if (idx < g_showTelemNames.size()) {
        auto& mgr = ConfigManager::instance();
        String newVal = g_showTelemNames[idx];
        if (mgr.config().messaging.showTelemetry != newVal) {
            mgr.config().messaging.showTelemetry = newVal;
            mgr.save();
        }
    }
    lv_async_call([](void* p) { ((MessagingSettingsScreen*)p)->hideShowTelemPicker(); }, self);
}

void MessagingSettingsScreen::hideShowTelemPicker() {
    if (!_showTelemBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    lv_obj_del_async(_showTelemBtnm);
    _showTelemBtnm = nullptr;
    if (_screen) show();
}

// ------------------------------------------------------------------
// Canned custom message editor
// ------------------------------------------------------------------

void MessagingSettingsScreen::cannedCustomRowCb(lv_event_t* e) {
    MessagingSettingsScreen* self = (MessagingSettingsScreen*)lv_event_get_user_data(e);
    if (!self || self->_cannedTextarea) return;

    lv_obj_t* row = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx < 0 || idx >= 8) return;
    self->_cannedEditIndex = idx;

    const auto& cfg = ConfigManager::instance().config();
    String current = (idx < (int)cfg.messaging.cannedCustom.size())
                         ? cfg.messaging.cannedCustom[idx]
                         : String("");

    self->_cannedOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_cannedOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_cannedOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_cannedOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(self->_cannedOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(self->_cannedOverlay, 0, 0);
    lv_obj_clear_flag(self->_cannedOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(self->_cannedOverlay);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    char titleBuf[32];
    snprintf(titleBuf, sizeof(titleBuf), "%s %d", t("lbl_canned_slot"), idx + 1);
    lv_label_set_text(lbl, titleBuf);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT);

    self->_cannedTextarea = lv_textarea_create(self->_cannedOverlay);
    lv_textarea_set_one_line(self->_cannedTextarea, true);
    lv_textarea_set_max_length(self->_cannedTextarea, 40);
    lv_textarea_set_placeholder_text(self->_cannedTextarea, t("lbl_canned_placeholder"));
    lv_textarea_set_text(self->_cannedTextarea, current.c_str());
    lv_obj_set_width(self->_cannedTextarea, theme::CONTENT_WIDTH);
    lv_obj_align(self->_cannedTextarea, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44);
    lv_obj_set_style_border_color(self->_cannedTextarea, theme::ACCENT(), LV_STATE_FOCUSED);

    lv_obj_t* btnRow = lv_obj_create(self->_cannedOverlay);
    lv_obj_set_size(btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, theme::PAD_MEDIUM, 0);
    lv_obj_align(btnRow, LV_ALIGN_TOP_MID, 0, theme::STATUS_BAR_HEIGHT + 44 + 52);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(save, theme::ACCENT(), 0);
    lv_obj_set_style_bg_color(save, theme::BG_SECONDARY(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(save, cannedReadyCb, LV_EVENT_CLICKED, self);
    lv_obj_t* saveLbl = lv_label_create(save);
    lv_label_set_text(saveLbl, t("btn_save"));
    lv_obj_center(saveLbl);

    lv_obj_t* cancel = lv_btn_create(btnRow);
    lv_obj_set_style_bg_color(cancel, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_color(cancel, theme::ACCENT(), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* s = static_cast<MessagingSettingsScreen*>(lv_event_get_user_data(ev));
        if (s) lv_async_call([](void* p) { ((MessagingSettingsScreen*)p)->hideCannedEditor(); }, s);
    }, LV_EVENT_CLICKED, self);
    lv_obj_t* cxlLbl = lv_label_create(cancel);
    lv_label_set_text(cxlLbl, t("btn_cancel"));
    lv_obj_center(cxlLbl);

    lv_group_t* g = lv_group_create();
    lv_group_add_obj(g, self->_cannedTextarea);
    lv_group_add_obj(g, save);
    lv_group_add_obj(g, cancel);
    lv_group_focus_obj(self->_cannedTextarea);
    UIManager::instance().switchToModalGroup(self->_cannedOverlay);
    IInput::instance().attachToGroup(g);
    lv_obj_add_event_cb(self->_cannedTextarea, cannedReadyCb, LV_EVENT_READY, self);

#ifdef PLATFORM_TWATCH
    self->_cannedKbd = lv_keyboard_create(self->_cannedOverlay);
    lv_keyboard_set_textarea(self->_cannedKbd, self->_cannedTextarea);
    lv_keyboard_set_popovers(self->_cannedKbd, true);
    lv_btnmatrix_set_btn_ctrl_all(self->_cannedKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(self->_cannedKbd, cannedReadyCb, LV_EVENT_READY, self);
    lv_obj_add_event_cb(self->_cannedKbd, [](lv_event_t* ev) {
        auto* self = static_cast<MessagingSettingsScreen*>(lv_event_get_user_data(ev));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(ev);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_btnmatrix_set_btn_ctrl_all(self->_cannedKbd, LV_BTNMATRIX_CTRL_NO_REPEAT);
        } else if (code == LV_EVENT_CANCEL) {
            lv_async_call([](void* p) { ((MessagingSettingsScreen*)p)->hideCannedEditor(); }, self);
        }
    }, LV_EVENT_ALL, self);
#endif
}

void MessagingSettingsScreen::cannedReadyCb(lv_event_t* e) {
    MessagingSettingsScreen* self = (MessagingSettingsScreen*)lv_event_get_user_data(e);
    if (!self || !self->_cannedTextarea || self->_cannedEditIndex < 0) return;
    const char* text = lv_textarea_get_text(self->_cannedTextarea);
    String newText = text ? String(text) : String("");
    // Trim whitespace
    {
        const char* s = newText.c_str();
        int len = strlen(s);
        int l = 0, r = len - 1;
        while (l <= r && isspace((unsigned char)s[l])) ++l;
        while (r >= l && isspace((unsigned char)s[r])) --r;
        if (l > 0 || r < len - 1) {
            newText = newText.substring(l, r + 1);
        }
    }
    if (newText.length() > 40) newText = newText.substring(0, 40);

    auto& mgr = ConfigManager::instance();
    auto& vec = mgr.config().messaging.cannedCustom;
    int idx = self->_cannedEditIndex;

    if (newText.length() > 0) {
        // Ensure vector is large enough
        while ((int)vec.size() <= idx) vec.push_back("");
        if (vec[idx] != newText) {
            vec[idx] = newText;
            mgr.save();
        }
    } else {
        if (idx < (int)vec.size() && vec[idx].length() > 0) {
            vec[idx] = "";
            mgr.save();
        }
    }
    self->_cannedEditIndex = -1;
    lv_async_call([](void* p) { ((MessagingSettingsScreen*)p)->hideCannedEditor(); }, self);
}

void MessagingSettingsScreen::hideCannedEditor() {
    if (!_cannedTextarea) return;
    UIManager::instance().restoreFromModalGroup();
#ifdef PLATFORM_TWATCH
    _cannedKbd = nullptr;
#endif
    _cannedTextarea = nullptr;
    lv_obj_del_async(_cannedOverlay);
    _cannedOverlay = nullptr;
    _cannedEditIndex = -1;
    if (_screen) show();
}

}  // namespace mclite
