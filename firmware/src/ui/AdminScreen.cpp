#include "AdminScreen.h"
#include <Arduino.h>
#include <time.h>
#include <vector>
#include "UIManager.h"
#include "theme.h"
#include "../config/ConfigManager.h"
#include "../hal/Display.h"
#include "../mesh/ContactStore.h"
#include "../mesh/ChannelStore.h"
#include "../mesh/MeshManager.h"
#include "../storage/HeardAdvertCache.h"
#include "../net/WiFiManager.h"
#include "../companion/CompanionService.h"
#include "../storage/MessageStore.h"
#include "../hal/Battery.h"
#include "../hal/GPS.h"
#include "../hal/Speaker.h"
#include "../config/defaults.h"
#include "../i18n/I18n.h"
#include "../util/TimeHelper.h"
#include "../util/offgrid.h"
#include "../util/locprecision.h"

namespace mclite {

namespace {
// Friendly name for a theme: translated label for built-ins, raw name for custom.
String themeDisplayName(const String& name) {
    if (name == "dark")          return t("theme_dark");
    if (name == "light")         return t("theme_light");
    if (name == "amber")         return t("theme_amber");
    if (name == "high_contrast") return t("theme_high_contrast");
    return name;  // custom theme — show its own name
}
}  // namespace

void AdminScreen::create(lv_obj_t* parent) {
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
    lv_obj_t* title = lv_win_add_title(_screen, t("admin_title"));
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

void AdminScreen::show() {
    if (!_screen) return;

    // Clear old content
    lv_obj_clean(_content);

    const auto& cfg = ConfigManager::instance().config();

    // Helper to add a row
    auto addRow = [this](const char* label, const String& value) {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, label);

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        lv_label_set_text(val, value.c_str());
    };

    // Helper for section headers
    auto addSection = [this](const char* title) {
        lv_obj_t* lbl = lv_label_create(_content);
        lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
        lv_obj_set_style_text_color(lbl, theme::ACCENT(), 0);
        lv_obj_set_style_pad_top(lbl, theme::PAD_MEDIUM, 0);
        lv_label_set_text(lbl, title);
    };

    // Offgrid toggle — promoted above all sections as the only interactive control.
    // Clickable row with tinted OFFGRID_ACCENT bg; tint depth signals state
    // (subtle when OFF, stronger when ON), distinct from BG_SECONDARY info rows.
    {
        lv_obj_t* row = lv_obj_create(_content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::OFFGRID_ACCENT(), 0);
        lv_obj_set_style_bg_opa(row, cfg.offgrid.enabled ? LV_OPA_50 : LV_OPA_20, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SMALL, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
        lv_label_set_text(lbl, t("lbl_offgrid"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        if (cfg.offgrid.enabled) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%s (%d MHz)",
                     t("offgrid_on"),
                     (int)mclite::offgridFreqFor(cfg.radio.frequency));
            lv_label_set_text(val, buf);
        } else {
            lv_label_set_text(val, t("offgrid_off"));
        }

        lv_obj_add_event_cb(row, offgridToggleCb, LV_EVENT_CLICKED, nullptr);
    }

    // Heard adverts shortcut — mirrors info-row styling but clickable, with chevron.
    {
        lv_obj_t* row = lv_obj_create(_content);
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

        _heardCountLabel = lv_label_create(row);
        lv_obj_set_style_text_font(_heardCountLabel, FONT_BODY, 0);
        lv_obj_set_style_text_color(_heardCountLabel, theme::TEXT_PRIMARY(), 0);
        char rowBuf[40];
        snprintf(rowBuf, sizeof(rowBuf), "%s (%d)",
                 t("heard_adverts_title"),
                 HeardAdvertCache::instance().count());
        lv_label_set_text(_heardCountLabel, rowBuf);
        _heardCacheVersion = HeardAdvertCache::instance().version();

        lv_obj_t* chev = lv_label_create(row);
        lv_obj_set_style_text_font(chev, FONT_BODY, 0);
        lv_obj_set_style_text_color(chev, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            UIManager::instance().showScreen(Screen::HEARD_ADVERTS);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // WiFi setup shortcut — shows the configured network (or "not configured"),
    // tap to scan/connect on-device.
    {
        lv_obj_t* row = lv_obj_create(_content);
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

        lv_obj_t* wl = lv_label_create(row);
        lv_obj_set_style_text_font(wl, FONT_BODY, 0);
        lv_obj_set_style_text_color(wl, theme::TEXT_PRIMARY(), 0);
        String wtxt = String(LV_SYMBOL_WIFI " ");
        if (WiFiManager::instance().isConnected()) {
            wtxt += WiFiManager::instance().connectedSsid();          // really connected
        } else {
            const String& ssid = ConfigManager::instance().config().wifi.ssid;
            wtxt += ssid.length() ? String(t("wifi_off"))            // saved but off
                                  : String(t("wifi_not_configured"));
        }
        lv_label_set_text(wl, wtxt.c_str());
        _wifiRowLabel = wl;
        _wifiLastConnected = WiFiManager::instance().isConnected();

        lv_obj_t* chev = lv_label_create(row);
        lv_obj_set_style_text_font(chev, FONT_BODY, 0);
        lv_obj_set_style_text_color(chev, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            UIManager::instance().showScreen(Screen::WIFI_SETUP);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // USB companion shortcut — opens a dedicated screen with just the toggle.
    {
        lv_obj_t* row = lv_obj_create(_content);
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

        lv_obj_t* ul = lv_label_create(row);
        lv_obj_set_style_text_font(ul, FONT_BODY, 0);
        lv_obj_set_style_text_color(ul, theme::TEXT_PRIMARY(), 0);
        String utxt = String(LV_SYMBOL_USB " ") + t("usb_companion");
        if (CompanionService::instance().usbCompanionEnabled()) utxt += String(" (") + t("on") + ")";
        lv_label_set_text(ul, utxt.c_str());

        lv_obj_t* chev = lv_label_create(row);
        lv_obj_set_style_text_font(chev, FONT_BODY, 0);
        lv_obj_set_style_text_color(chev, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            UIManager::instance().showScreen(Screen::USB_SETUP);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // BLE companion shortcut — opens a dedicated screen with the toggle + PIN.
    {
        lv_obj_t* row = lv_obj_create(_content);
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

        lv_obj_t* bl = lv_label_create(row);
        lv_obj_set_style_text_font(bl, FONT_BODY, 0);
        lv_obj_set_style_text_color(bl, theme::TEXT_PRIMARY(), 0);
        String btxt = String(LV_SYMBOL_BLUETOOTH " ") + t("ble_companion");
        if (CompanionService::instance().bleCompanionEnabled()) btxt += String(" (") + t("on") + ")";
        lv_label_set_text(bl, btxt.c_str());

        lv_obj_t* chev = lv_label_create(row);
        lv_obj_set_style_text_font(chev, FONT_BODY, 0);
        lv_obj_set_style_text_color(chev, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            UIManager::instance().showScreen(Screen::BLE_SETUP);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // --- Device ---
    addSection(t("sec_device"));
    addRow(t("lbl_firmware"), String("MCLite v") + defaults::FIRMWARE_VERSION);
    addRow(t("lbl_vendor"), defaults::FIRMWARE_VENDOR);
    addRow(t("lbl_built"), String(__DATE__) + " " + __TIME__);
    addRow(t("lbl_device_name"), cfg.deviceName);
    if (cfg.publicKey.length() > 0) {
        addRow(t("lbl_public_key"), cfg.publicKey.substring(0, 16) + "...");
    }

    // --- Radio ---
    addSection(t("sec_radio"));
    {
        // Show the active frequency: in offgrid mode this is the derived band,
        // with "(offgrid)" marker so users see 869.000 (offgrid) vs 869.618 at a glance.
        float activeFreq = cfg.radio.frequency;
        String freqSuffix = " MHz";
        if (cfg.offgrid.enabled) {
            activeFreq = mclite::offgridFreqFor(cfg.radio.frequency);
            freqSuffix += " (offgrid)";
        }
        addRow(t("lbl_frequency"), String(activeFreq, 3) + freqSuffix);
    }
    addRow(t("lbl_sf_bw"), String(cfg.radio.spreadingFactor) + " / " + String(cfg.radio.bandwidth, 1));
    addRow(t("lbl_coding_rate"), String(cfg.radio.codingRate));
    addRow(t("lbl_tx_power"), String(cfg.radio.txPower) + " dBm");
    addRow(t("lbl_scope"), cfg.radio.scope);
    {
        char phBuf[16];
        snprintf(phBuf, sizeof(phBuf), "%u B/hop", (unsigned)(cfg.radio.pathHashMode + 1));
        addRow(t("lbl_path_hash"), phBuf);
    }
    addRow(t("lbl_status"), MeshManager::instance().isRadioReady() ? t("ready") : t("error"));

    // Channel utilization (TX duty cycle over last hour)
    if (MeshManager::instance().isRadioReady()) {
        float duty = MeshManager::instance().getTxDutyCyclePercent();
        char utilBuf[32];
        if (MeshManager::instance().isEURegion()) {
            snprintf(utilBuf, sizeof(utilBuf), "%.2f%% (10%% limit)", duty);
        } else {
            snprintf(utilBuf, sizeof(utilBuf), "%.2f%%", duty);
        }
        addRow(t("ch_util"), utilBuf);
    }

    // --- Contacts ---
    auto& contacts = ContactStore::instance();
    char secContactsBuf[32];
    snprintf(secContactsBuf, sizeof(secContactsBuf), t("sec_contacts"), (int)contacts.count());
    addSection(secContactsBuf);
    for (size_t i = 0; i < contacts.count(); i++) {
        const Contact* c = contacts.findByIndex(i);
        if (!c) continue;
        String info = c->name;
        if (c->sendSos) info += " [SOS]";
        if (c->allowTelemetry && c->allowLocation) info += " [GPS]";
        addRow(("  " + String((int)(i + 1))).c_str(), info);
    }

    // --- Channels ---
    auto& channels = ChannelStore::instance();
    char secChannelsBuf[32];
    snprintf(secChannelsBuf, sizeof(secChannelsBuf), t("sec_channels"), (int)channels.count());
    addSection(secChannelsBuf);
    for (const auto& ch : channels.all()) {
        const char* prefix = ch.isPrivate() ? "  *" : "  #";
        String info = ch.name;
        if (ch.readOnly) info += " [read-only]";
        if (ch.sendSos) info += " [SOS]";
        if (ch.scope.length() > 0) info += " [scope:" + ch.scope + "]";
        addRow(prefix, info);
    }

    // --- Rooms (read-only — config tool manages add/remove) ---
    {
        const auto& rooms = ConfigManager::instance().config().roomServers;
        char secRoomsBuf[32];
        snprintf(secRoomsBuf, sizeof(secRoomsBuf), t("sec_rooms"), (int)rooms.size());
        addSection(secRoomsBuf);
        auto& store = MessageStore::instance();
        for (size_t i = 0; i < rooms.size(); i++) {
            String info = rooms[i].name;
            info += UIManager::instance().isRoomLoggedIn(i) ? " [online]" : " [offline]";
            // Last sync timestamp (Unix seconds) from the room's history
            if (rooms[i].publicKey.length() == 64) {
                String shortId = rooms[i].publicKey.substring(0, 16);
                ConvoId rid { ConvoId::ROOM, shortId };
                if (Conversation* convo = store.getConversation(rid)) {
                    if (convo->syncSince > 0) {
                        char ts[24];
                        time_t t = (time_t)convo->syncSince;
                        struct tm* tm_info = gmtime(&t);
                        if (tm_info) {
                            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm_info);
                            info += " ";
                            info += ts;
                        }
                    }
                }
            }
            addRow("  R", info);
        }
    }

    // --- Display ---
    addSection(t("sec_display"));

    // Theme picker — clickable row; opens a chooser and reboots to apply.
    {
        lv_obj_t* row = lv_obj_create(_content);
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

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, theme::TEXT_SECONDARY(), 0);
        lv_label_set_text(lbl, t("lbl_theme"));

        lv_obj_t* val = lv_label_create(row);
        lv_obj_set_style_text_font(val, FONT_BODY, 0);
        lv_obj_set_style_text_color(val, theme::TEXT_PRIMARY(), 0);
        lv_label_set_text(val, (themeDisplayName(cfg.display.theme) + "  " LV_SYMBOL_RIGHT).c_str());

        lv_obj_add_event_cb(row, themeRowCb, LV_EVENT_CLICKED, this);
    }

    addRow(t("lbl_brightness"), String(cfg.display.brightness));
    addRow(t("lbl_auto_dim"), cfg.display.autoDimSeconds > 0
        ? String(cfg.display.autoDimSeconds) + "s" : String(t("off")));
    addRow(t("lbl_dim_brightness"), cfg.display.dimBrightness > 0
        ? String(cfg.display.dimBrightness) : String(t("off")));
    addRow(t("lbl_kbd_backlight"), cfg.display.kbdBacklight
        ? String(t("on")) + " (" + String(cfg.display.kbdBrightness) + ")" : String(t("off")));
    addRow(t("lbl_emoji"), cfg.display.emoji ? t("on") : t("off"));
    addRow(t("lbl_screenshots"), cfg.debug.screenshots ? t("on") : t("off"));
    if (cfg.display.bootText.length() > 0) {
        addRow(t("lbl_boot_text"), cfg.display.bootText);
    }

    // --- Messaging ---
    addSection(t("sec_messaging"));
    addRow(t("lbl_history"), cfg.messaging.saveHistory ? t("enabled") : t("disabled"));
    addRow(t("lbl_max_per_chat"), String(cfg.messaging.maxHistoryPerChat));
    addRow(t("lbl_max_retries"), String(cfg.messaging.maxRetries));
    addRow(t("lbl_req_telemetry"), cfg.messaging.requestTelemetry ? t("enabled") : t("disabled"));
    addRow(t("lbl_telemetry_badges"), cfg.messaging.showTelemetry);
    addRow(t("lbl_auto_telemetry"), cfg.messaging.autoTelemetry ? t("on") : t("off"));

    // --- GPS ---
    addSection(t("sec_gps"));
    addRow(t("lbl_gps"), cfg.gpsEnabled ? t("enabled") : t("disabled"));
    if (cfg.gpsEnabled) {
        auto& gps = GPS::instance();
        FixStatus fs = gps.fixStatus();
        switch (fs) {
            case FixStatus::LIVE: {
                addRow(t("gps_fix_status"), t("gps_live"));
                addRow(t("gps_coords"), gps.formatLocation());
                addRow(t("gps_satellites"), String(gps.satellites()));
                char hdopBuf[8];
                snprintf(hdopBuf, sizeof(hdopBuf), "%.1f", gps.hdop());
                addRow(t("lbl_hdop"), String(hdopBuf));
                break;
            }
            case FixStatus::LAST_KNOWN: {
                uint32_t age = gps.fixAgeSeconds();
                char ageBuf[32];
                if (age < 60)
                    snprintf(ageBuf, sizeof(ageBuf), t("gps_last_known_s"), (int)age);
                else if (age < 3600)
                    snprintf(ageBuf, sizeof(ageBuf), t("gps_last_known_m"), (int)(age / 60));
                else
                    snprintf(ageBuf, sizeof(ageBuf), t("gps_last_known_h"), (int)(age / 3600));
                addRow(t("gps_fix_status"), String(ageBuf));
                addRow(t("gps_coords"), gps.formatLocation());
                break;
            }
            case FixStatus::NO_FIX:
                addRow(t("gps_fix_status"), t("searching"));
                break;
        }
        addRow(t("gps_coord_format"), cfg.messaging.locationFormat);
        addRow(t("lbl_last_known_max"), String(cfg.gpsLastKnownMaxAge) + "s");
        {
            String locVal;
            if (cfg.locationPrecision == 0)       locVal = t("off");
            else if (cfg.locationPrecision >= 32) locVal = t("loc_exact");
            // Friendly labels matching the config tool's precision dropdown.
            else if (cfg.locationPrecision == 19) locVal = "~100 m";
            else if (cfg.locationPrecision == 16) locVal = "~750 m";
            else if (cfg.locationPrecision == 14) locVal = "~3 km";
            else if (cfg.locationPrecision == 12) locVal = "~12 km";
            else if (cfg.locationPrecision == 10) locVal = "~50 km";
            else {  // arbitrary precision set via config.json — compute it
                uint32_t m = locPrecisionMeters(cfg.locationPrecision);
                locVal = (m >= 1000) ? ("~" + String(m / 1000) + " km") : ("~" + String(m) + " m");
            }
            addRow(t("lbl_location_advert"), locVal);
        }
        if (cfg.gpsTimezone.length() > 0 && TimeHelper::isValidPosixTz(cfg.gpsTimezone)) {
            // Show abbreviation (chars before first digit/sign) + "(auto-DST)"
            String abbr;
            for (size_t i = 0; i < cfg.gpsTimezone.length(); i++) {
                char c = cfg.gpsTimezone[i];
                if (c == '-' || c == '+' || (c >= '0' && c <= '9')) break;
                abbr += c;
            }
            addRow(t("lbl_timezone"), abbr + " (auto-DST)");
        } else if (cfg.gpsClockOffset != 0) {
            addRow(t("lbl_clock_offset"), String(cfg.gpsClockOffset) + "h");
        }
    }

    // --- Sound ---
    addSection(t("sec_sound"));
    addRow(t("lbl_sound"), Speaker::instance().isMuted() ? t("muted") : t("on"));
    addRow(t("lbl_sos_keyword"), cfg.sosKeyword);
    addRow(t("lbl_sos_repeat"), String(cfg.sosRepeat));

    // --- Battery ---
    addSection(t("sec_battery"));
    addRow(t("lbl_level"), String(Battery::instance().percent()) + "%");
    if (cfg.battery.lowAlertEnabled) {
        addRow(t("lbl_low_alert"), String(cfg.battery.lowAlertThreshold) + "%");
    } else {
        addRow(t("lbl_low_alert"), t("off"));
    }

    // Uptime — wall-clock boot time + relative ago.
    {
        uint32_t bootEp = TimeHelper::instance().bootEpoch();
        char tsBuf[20], agoBuf[16];
        TimeHelper::instance().formatTimestamp(bootEp, tsBuf, sizeof(tsBuf));
        uint32_t nowEp = TimeHelper::instance().bestEpoch();
        if (bootEp >= 1700000000 && nowEp > bootEp) {
            TimeHelper::formatAgo(nowEp - bootEp, agoBuf, sizeof(agoBuf));
        } else {
            strncpy(agoBuf, "--", sizeof(agoBuf));
            agoBuf[sizeof(agoBuf) - 1] = '\0';
        }
        char rowBuf[48];
        snprintf(rowBuf, sizeof(rowBuf), "%s (%s)", tsBuf, agoBuf);
        addRow(t("lbl_uptime"), rowBuf);
    }

    // Last charged — timestamp when charging last stopped + relative ago + level.
    {
        auto& batt = Battery::instance();
        if (!batt.isCharging()) {
            uint32_t lcEp = batt.lastChargedEpoch();
            if (lcEp >= 1700000000) {
                char tsBuf[20], agoBuf[16];
                TimeHelper::instance().formatTimestamp(lcEp, tsBuf, sizeof(tsBuf));
                uint32_t nowEp = TimeHelper::instance().bestEpoch();
                if (nowEp >= lcEp) {
                    TimeHelper::formatAgo(nowEp - lcEp, agoBuf, sizeof(agoBuf));
                } else {
                    strncpy(agoBuf, "--", sizeof(agoBuf));
                    agoBuf[sizeof(agoBuf) - 1] = '\0';
                }
                char rowBuf[48];
                snprintf(rowBuf, sizeof(rowBuf), "%s (%s, %d%%)",
                        tsBuf, agoBuf, (int)batt.lastChargedPercent());
                addRow(t("lbl_last_charged"), rowBuf);
            }
        }
    }

    // --- Security ---
    addSection(t("sec_security"));
    String lockLabel = t("off");
    if (cfg.security.lockMode == "key") lockLabel = t("lock_key");
    else if (cfg.security.lockMode == "pin") lockLabel = t("lock_pin");
    addRow(t("lbl_lock"), lockLabel);

    // --- Language ---
    addSection(t("sec_language"));
    addRow(t("lang_current"), I18n::instance().currentLanguage());
    addRow(t("lang_available"), I18n::instance().availableLanguages());

    // --- Licenses ---
    addSection(t("sec_licenses"));
    addRow("MCLite", "MIT");

    // Expandable 3rd-party licenses
    lv_obj_t* licToggle = lv_label_create(_content);
    lv_obj_set_style_text_font(licToggle, FONT_BODY, 0);
    lv_obj_set_style_text_color(licToggle, theme::ACCENT(), 0);
    lv_obj_add_flag(licToggle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_top(licToggle, theme::PAD_SMALL, 0);
    String licToggleText = String(LV_SYMBOL_RIGHT " ") + t("licenses_toggle");
    lv_label_set_text(licToggle, licToggleText.c_str());

    lv_obj_t* licContainer = lv_obj_create(_content);
    lv_obj_set_size(licContainer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(licContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(licContainer, 0, 0);
    lv_obj_set_style_pad_all(licContainer, 0, 0);
    lv_obj_set_style_pad_row(licContainer, 2, 0);
    lv_obj_set_flex_flow(licContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(licContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(licContainer, LV_OBJ_FLAG_HIDDEN);

    // Full copyright notices (MIT requires copyright + permission notice)
    static const char* licenseText =
        "LVGL 8.4.0 - MIT\n"
        "(c) 2021 LVGL Kft\n\n"
        "LovyanGFX 1.2.19 - MIT + BSD-2-Clause\n"
        "(c) lovyan03\n\n"
        "ArduinoJson 7.4.3 - MIT\n"
        "(c) 2014-2026 Benoit Blanchon\n\n"
        "RadioLib 7.3.0 - MIT\n"
        "(c) 2018 Jan Gromes\n\n"
        "MeshCore 1.10.0 - MIT\n"
        "(c) 2025 Scott Powell / rippleradios.com\n\n"
        "base64 1.4.0 - MIT\n"
        "(c) 2016 Densaugeo\n\n"
        "PNGdec 1.0.3 - Apache-2.0\n"
        "(c) 2020-2024 Larry Bank\n\n"
        "orlp/ed25519 - Zlib\n"
        "(c) 2015 Orson Peters\n\n"
        "Crypto 0.4.0 - MIT\n"
        "(c) 2015, 2018 Southern Storm Software\n\n"
        "RTClib 2.1.4 - MIT\n"
        "(c) 2019 Adafruit Industries\n\n"
        "Adafruit BusIO 1.17.4 - MIT\n"
        "(c) 2017 Adafruit Industries\n\n"
        "CayenneLPP 1.6.1 - MIT\n"
        "(c) Electronic Cats\n\n"
        "Melopero RV3028 1.2.0 - MIT\n"
        "(c) 2020 Melopero\n\n"
        "TinyGPSPlus 1.1.0 - LGPL-2.1\n"
        "(c) 2008-2024 Mikal Hart\n\n"
        "Arduino ESP32 core - LGPL-2.1\n"
        "(c) Espressif\n\n"
        "OpenMoji emoji font - CC-BY-SA 4.0\n"
        "(c) OpenMoji project (hfg-gmuend)\n\n"
        "MIT/Apache-2.0/Zlib libraries are used under\n"
        "the terms of those licenses. LGPL-2.1 libraries\n"
        "are dynamically linked; users may replace them\n"
        "by rebuilding with PlatformIO.\n\n"
        "Full license texts: see LICENSES.md";

    lv_obj_t* licLabel = lv_label_create(licContainer);
    lv_obj_set_style_text_font(licLabel, FONT_BODY, 0);
    lv_obj_set_style_text_color(licLabel, theme::TEXT_SECONDARY(), 0);
    lv_obj_set_width(licLabel, LV_PCT(100));
    lv_label_set_long_mode(licLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text_static(licLabel, licenseText);

    // Toggle callback
    lv_obj_add_event_cb(licToggle, [](lv_event_t* e) {
        lv_obj_t* toggle = lv_event_get_target(e);
        lv_obj_t* container = (lv_obj_t*)lv_event_get_user_data(e);
        bool hidden = lv_obj_has_flag(container, LV_OBJ_FLAG_HIDDEN);
        if (hidden) {
            lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
            String txt = String(LV_SYMBOL_DOWN " ") + t("licenses_toggle");
            lv_label_set_text(toggle, txt.c_str());
        } else {
            lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
            String txt = String(LV_SYMBOL_RIGHT " ") + t("licenses_toggle");
            lv_label_set_text(toggle, txt.c_str());
        }
    }, LV_EVENT_CLICKED, licContainer);

    // Footer
    lv_obj_t* footer = lv_label_create(_content);
    lv_obj_set_style_text_font(footer, FONT_BODY, 0);
    lv_obj_set_style_text_color(footer, theme::TEXT_TIMESTAMP(), 0);
    lv_obj_set_style_pad_top(footer, theme::PAD_MEDIUM, 0);
    lv_label_set_text(footer, t("admin_footer"));

    // Add content to input group so trackball can scroll
    lv_group_t* grp = lv_group_get_default();
    if (grp) {
        lv_group_add_obj(grp, _backBtn);
        lv_group_add_obj(grp, _content);
        lv_group_focus_obj(_content);
        lv_group_set_editing(grp, true);
    }

    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

void AdminScreen::backBtnCb(lv_event_t* e) {
    UIManager::instance().goHome();
}

void AdminScreen::offgridToggleCb(lv_event_t* e) {
    // Build the confirm-dialog text with the current derived freq.
    const auto& cfg = ConfigManager::instance().config();
    bool enabling = !cfg.offgrid.enabled;

    float og = mclite::offgridFreqFor(cfg.radio.frequency);

    static char bodyBuf[192];
    if (enabling) {
        snprintf(bodyBuf, sizeof(bodyBuf), t("offgrid_confirm_on_body"), (int)og);
    } else {
        snprintf(bodyBuf, sizeof(bodyBuf), t("offgrid_confirm_off_body"), cfg.radio.frequency);
    }

    static const char* btns[3];
    btns[0] = t("btn_cancel");
    btns[1] = t("reboot_now");
    btns[2] = "";

    const char* title = enabling ? t("offgrid_confirm_on_title") : t("offgrid_confirm_off_title");

    lv_obj_t* msgbox = lv_msgbox_create(NULL, title, bodyBuf, btns, false);
    lv_obj_center(msgbox);
    lv_obj_set_style_bg_color(msgbox, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_text_color(msgbox, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_text_font(msgbox, FONT_HEADING, 0);

    lv_obj_t* btnm = lv_msgbox_get_btns(msgbox);
    if (btnm) UIManager::instance().switchToModalGroup(btnm);

    lv_obj_add_event_cb(msgbox, [](lv_event_t* ev) {
        lv_obj_t* mbox = lv_event_get_current_target(ev);
        uint16_t btnIdx = lv_msgbox_get_active_btn(mbox);
        if (btnIdx == LV_BTNMATRIX_BTN_NONE) return;

        if (btnIdx == 1) {
            // "Reboot now" — flip the flag, persist, restart.
            auto& mgr = ConfigManager::instance();
            mgr.config().offgrid.enabled = !mgr.config().offgrid.enabled;
            mgr.save();
            UIManager::instance().restoreFromModalGroup();
            lv_msgbox_close(mbox);
            delay(200);
            ESP.restart();
            return;
        }

        UIManager::instance().restoreFromModalGroup();
        lv_msgbox_close(mbox);
    }, LV_EVENT_VALUE_CHANGED, NULL);
}

// Theme chooser state. File-scope so the static event callbacks can reach it:
// g_themeNames holds the canonical names parallel to the buttons (Cancel is the
// last button, index == size); g_themeMap is the live btnmatrix map (must persist
// while the matrix is alive); g_themeLabels keeps the label strings alive.
namespace {
std::vector<String>      g_themeNames;
std::vector<String>      g_themeLabels;
std::vector<const char*> g_themeMap;
}  // namespace

void AdminScreen::themeRowCb(lv_event_t* e) {
    AdminScreen* self = (AdminScreen*)lv_event_get_user_data(e);
    if (!self || self->_themeBtnm) return;   // already open
    const auto& cfg = ConfigManager::instance().config();

    // Available themes: built-ins first, then any custom names from config.
    g_themeNames.clear(); g_themeLabels.clear(); g_themeMap.clear();
    static const char* const BUILTIN[] = {"dark", "light", "amber", "high_contrast"};
    for (const char* b : BUILTIN) g_themeNames.push_back(b);
    for (const auto& ct : cfg.display.customThemes) g_themeNames.push_back(ct.name);

    for (const auto& n : g_themeNames) g_themeLabels.push_back(themeDisplayName(n));
    g_themeLabels.push_back(t("btn_cancel"));   // Cancel = last button

    // One button per row: "\n" between labels (a column layout, like the canned
    // picker). "\n" entries are not counted as buttons, so the selected index
    // still maps to g_themeNames (Cancel index == g_themeNames.size()).
    for (size_t i = 0; i < g_themeLabels.size(); i++) {
        if (i > 0) g_themeMap.push_back("\n");
        g_themeMap.push_back(g_themeLabels[i].c_str());
    }
    g_themeMap.push_back("");   // sentinel
    int count = (int)g_themeLabels.size();

    // Scrim overlay — tap to dismiss. Parented to the top layer (NOT the Admin
    // screen): the Admin screen is an lv_win with a flex layout, so a child added
    // there would be laid out as a flex item off-screen instead of floating.
    self->_themeOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(self->_themeOverlay, Display::width(), Display::height());
    lv_obj_set_pos(self->_themeOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_themeOverlay, theme::SCRIM(), 0);
    lv_obj_set_style_bg_opa(self->_themeOverlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(self->_themeOverlay, 0, 0);
    lv_obj_clear_flag(self->_themeOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(self->_themeOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(self->_themeOverlay, [](lv_event_t* ev) {
        AdminScreen* s = (AdminScreen*)lv_event_get_user_data(ev);
        lv_async_call([](void* ctx) { ((AdminScreen*)ctx)->hideThemePicker(); }, s);
    }, LV_EVENT_CLICKED, self);

    // Button matrix — one theme per full-width row (top layer, above the win).
    self->_themeBtnm = lv_btnmatrix_create(lv_layer_top());
    lv_btnmatrix_set_map(self->_themeBtnm, g_themeMap.data());
#ifdef PLATFORM_TWATCH
    lv_coord_t rowH = 64;
#else
    lv_coord_t rowH = 26;
#endif
    lv_coord_t pickerH = count * rowH + 8;
    lv_coord_t maxH = Display::height() - theme::STATUS_BAR_HEIGHT - theme::FOOTER_HEIGHT - 16;
    if (pickerH > maxH) pickerH = maxH;
    lv_obj_set_size(self->_themeBtnm, theme::MODAL_TEXT_WIDTH, pickerH);
    lv_obj_align(self->_themeBtnm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(self->_themeBtnm, FONT_HEADING, 0);
    lv_obj_set_style_bg_color(self->_themeBtnm, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(self->_themeBtnm, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(self->_themeBtnm, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(self->_themeBtnm, 1, 0);
    lv_obj_set_style_radius(self->_themeBtnm, 8, 0);
    lv_obj_set_style_bg_color(self->_themeBtnm, theme::BG_INPUT(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(self->_themeBtnm, theme::TEXT_PRIMARY(), LV_PART_ITEMS);
    lv_obj_set_style_radius(self->_themeBtnm, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(self->_themeBtnm, theme::ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(self->_themeBtnm, theme::TEXT_ON_ACCENT(), LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(self->_themeBtnm, [](lv_event_t* ev) {
        AdminScreen* s = (AdminScreen*)lv_event_get_user_data(ev);
        uint16_t idx = lv_btnmatrix_get_selected_btn(s->_themeBtnm);
        if (idx == LV_BTNMATRIX_BTN_NONE) return;

        if (idx < g_themeNames.size()) {   // a theme (Cancel sits after them)
            auto& mgr = ConfigManager::instance();
            if (mgr.config().display.theme != g_themeNames[idx]) {
                mgr.config().display.theme = g_themeNames[idx];
                mgr.save();
                delay(200);
                ESP.restart();   // inline styles bake the color in → repaint via reboot
                return;
            }
        }
        // Cancel or same theme → just dismiss (async; can't delete mid-event).
        lv_async_call([](void* ctx) { ((AdminScreen*)ctx)->hideThemePicker(); }, s);
    }, LV_EVENT_VALUE_CHANGED, self);

    UIManager::instance().switchToModalGroup(self->_themeBtnm);
}

void AdminScreen::hideThemePicker() {
    if (!_themeBtnm) return;
    UIManager::instance().restoreFromModalGroup();
    lv_obj_del_async(_themeBtnm);    _themeBtnm = nullptr;
    lv_obj_del_async(_themeOverlay); _themeOverlay = nullptr;
}

void AdminScreen::hide() {
    if (_screen) {
        if (_themeBtnm) hideThemePicker();   // tear down an open picker first
        lv_group_t* grp = lv_group_get_default();
        if (grp) {
            lv_group_set_editing(grp, false);
            lv_group_remove_obj(_backBtn);
            lv_group_remove_obj(_content);
        }
        // _heardCountLabel lives inside the row that show() recreates each visit,
        // so the pointer is dead until next show(). Drop it now to avoid a
        // dangling deref from tick() if something else paints over it.
        _heardCountLabel = nullptr;
        _uptimeLabel = nullptr;
        _lastChargedLabel = nullptr;
        lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    }
}

void AdminScreen::tick() {
    if (!_screen || lv_obj_has_flag(_screen, LV_OBJ_FLAG_HIDDEN)) return;

    // WiFi row — refresh when the connection state changes
    if (_wifiRowLabel) {
        bool c = WiFiManager::instance().isConnected();
        if (c != _wifiLastConnected) {
            _wifiLastConnected = c;
            String wtxt = String(LV_SYMBOL_WIFI " ");
            if (c) {
                wtxt += WiFiManager::instance().connectedSsid();
            } else {
                const String& ssid = ConfigManager::instance().config().wifi.ssid;
                wtxt += ssid.length() ? String(t("wifi_off"))
                                      : String(t("wifi_not_configured"));
            }
            lv_label_set_text(_wifiRowLabel, wtxt.c_str());
        }
    }

    // Heard-adverts count
    if (_heardCountLabel) {
        uint32_t v = HeardAdvertCache::instance().version();
        if (v != _heardCacheVersion) {
            _heardCacheVersion = v;
            char rowBuf[40];
            snprintf(rowBuf, sizeof(rowBuf), "%s (%d)",
                     t("heard_adverts_title"),
                     HeardAdvertCache::instance().count());
            lv_label_set_text(_heardCountLabel, rowBuf);
        }
    }
}

}  // namespace mclite
