#include "I18n.h"
#include "strings.h"
#include "../storage/SDCard.h"
#include <ArduinoJson.h>
#include <SD.h>

namespace mclite {

const DefaultString DEFAULT_STRINGS[] = {
    // Setup screens
    {"no_sd_title",         "No SD Card"},
    {"no_sd_msg",           "Insert an SD card with config.json\nand reboot.\n\nUse the MCLite Config Tool to\ncreate your configuration."},
    {"setup_title",         "Setup Required"},
    {"setup_msg",           "Default config.json created on SD.\n\nUse the MCLite Config Tool to set\nidentity, contacts and channels.\nCopy config.json to SD and reboot."},
    {"config_error_title",  "Config Error"},
    {"config_error_msg",    "config.json is corrupt or invalid.\n\nUse the MCLite Config Tool to fix\nyour configuration.\nCopy config.json to SD and reboot."},

    // SOS
    {"btn_dismiss",         "Dismiss"},
    {"btn_sos_seen",        "SOS seen"},
    {"sos_alert_title",     "SOS ALERT"},
    {"sos_from",            "From: %s"},
    {"sos_ack",             "SOS acknowledged"},
    {"sos_countdown",       "Sending SOS in %d..."},
    {"sos_sent",            "SOS sent to %d recipient(s)"},
    {"sos_sent_title",      "SOS Sent"},
    {"btn_ok",              "OK"},
    {"battery_low",         "LOW BATTERY: %d%%"},

    // PIN lock
    {"pin_title",           "Enter PIN"},
    {"pin_hint",            "Enter your PIN code"},
    {"pin_wrong",           "Incorrect PIN"},

    // Chat
    {"chat_placeholder",    "Type message..."},
    {"btn_send",            "SEND"},
    {"location_title",      "Send Location?"},
    {"btn_cancel",          "Cancel"},
    {"btn_location_send",   "Send"},
    {"loc_last_known_s",    "Last known position (~%ds ago)"},
    {"loc_last_known_m",    "Last known position (~%dm ago)"},
    {"loc_last_known_h",    "Last known position (~%dh ago)"},

    // Canned messages (quick replies)
    {"canned_1",            "OK"},
    {"canned_2",            "Copy"},
    {"canned_3",            "Where are you?"},
    {"canned_4",            "Please respond"},
    {"canned_5",            "En route"},
    {"canned_6",            "Stand by"},
    {"canned_7",            "Need help"},
    {"canned_8",            "All clear"},

    // Convo list
    {"no_contacts",         "No contacts configured.\nEdit config.json on SD card."},
    {"time_s",              "%ds"},
    {"time_m",              "%dm"},
    {"time_h",              "%dh"},
    {"time_d",              "%dd"},

    // Admin screen
    {"admin_title",         "Device Info (read-only)"},
    {"sec_device",          "Device"},
    {"sec_radio",           "Radio"},
    {"sec_contacts",        "Contacts (%d)"},
    {"sec_channels",        "Channels (%d)"},
    {"sec_display",         "Display"},
    {"sec_messaging",       "Messaging"},
    {"sec_gps",             "GPS"},
    {"sec_sound",           "Sound"},
    {"sec_battery",         "Battery"},
    {"sec_security",        "Security"},
    {"sec_licenses",        "Licenses"},
    {"ch_util",             "Ch. Util"},
    {"ready",               "Ready"},
    {"error",               "Error"},
    {"enabled",             "Enabled"},
    {"disabled",            "Disabled"},
    {"off",                 "Off"},
    {"on",                  "On"},
    {"muted",               "Muted"},
    {"searching",           "Searching..."},
    {"gps_fix_status",      "Fix"},
    {"gps_live",            "Live"},
    {"gps_last_known_s",    "Last known (~%ds ago)"},
    {"gps_last_known_m",    "Last known (~%dm ago)"},
    {"gps_last_known_h",    "Last known (~%dh ago)"},
    {"gps_coords",          "Coordinates"},
    {"gps_satellites",      "Satellites"},
    {"gps_coord_format",    "Coord Format"},
    {"sec_language",        "Language"},
    {"lang_current",        "Current"},
    {"lang_available",      "Available"},
    {"licenses_toggle",     "3rd-party licenses"},
    {"admin_footer",        "Edit config.json on SD card to change settings"},

    // Telemetry
    {"telem_title",         "Contact Info"},
    {"telem_battery",       "Battery: %.2fV (~%d%%)"},
    {"telem_location",      "Location: %s"},
    {"telem_distance",      "Distance: %s"},
    {"telem_environment",   "Environment: %s"},
    {"telem_updated",       "Updated %s ago"},
    {"telem_no_data",       "No telemetry data yet"},
    {"telem_requesting",    "Requesting..."},
    {"telem_no_response",   "No response"},
    {"telem_send_failed",   "Request failed"},
    {"telem_stale",         "(data may be outdated)"},
    {"btn_refresh",         "Refresh"},
    {"btn_close",           "Close"},

    {nullptr, nullptr}  // sentinel
};

I18n& I18n::instance() {
    static I18n inst;
    return inst;
}

void I18n::init(const String& langCode) {
    // Scan available translation files
    _availableLangs = "en";
    File langDir = SD.open("/mclite/lang");
    if (langDir && langDir.isDirectory()) {
        File f = langDir.openNextFile();
        while (f) {
            String name = f.name();
            f.close();
            // f.name() may return full path (e.g. "/mclite/lang/de.json")
            int lastSlash = name.lastIndexOf('/');
            if (lastSlash >= 0) name = name.substring(lastSlash + 1);
            if (name.endsWith(".json") && !name.startsWith("._")) {
                _availableLangs += ", " + name.substring(0, name.length() - 5);
            }
            f = langDir.openNextFile();
        }
        langDir.close();
    }

    if (langCode.isEmpty()) {
        _currentLang = "en";
        Serial.println("[I18n] Using English (default)");
        return;
    }

    _currentLang = langCode;

    String path = "/mclite/lang/" + langCode + ".json";
    auto& sd = SDCard::instance();

    if (!sd.isMounted() || !sd.fileExists(path.c_str())) {
        Serial.printf("[I18n] Translation file %s not found, using English\n", path.c_str());
        return;
    }

    String json = sd.readFile(path.c_str());
    if (json.isEmpty()) {
        Serial.println("[I18n] Empty translation file, using English");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[I18n] Parse error: %s, using English\n", err.c_str());
        return;
    }

    JsonObject obj = doc.as<JsonObject>();
    _count = 0;

    // First pass: measure total buffer size needed for all keys + values
    size_t totalLen = 0;
    for (JsonPair kv : obj) {
        if (!kv.value().is<const char*>()) continue;
        totalLen += strlen(kv.key().c_str()) + 1;
        totalLen += strlen(kv.value().as<const char*>()) + 1;
    }

    // Single allocation for all strings (avoids heap fragmentation)
    free(_jsonBuf);  // Free previous buffer if init() called again
    _count = 0;
    _jsonBuf = (char*)malloc(totalLen);
    if (!_jsonBuf) {
        Serial.println("[I18n] Out of memory for translation");
        return;
    }

    // Second pass: copy strings into our buffer
    char* cursor = _jsonBuf;
    for (JsonPair kv : obj) {
        if (_count >= MAX_STRINGS) break;
        if (!kv.value().is<const char*>()) continue;

        const char* k = kv.key().c_str();
        size_t kLen = strlen(k) + 1;
        memcpy(cursor, k, kLen);
        _entries[_count].key = cursor;
        cursor += kLen;

        const char* v = kv.value().as<const char*>();
        size_t vLen = strlen(v) + 1;
        memcpy(cursor, v, vLen);
        _entries[_count].value = cursor;
        cursor += vLen;

        _count++;
    }

    Serial.printf("[I18n] Loaded %d strings for '%s'\n", _count, langCode.c_str());
}

const char* I18n::t(const char* key) {
    // Check loaded translations first
    for (size_t i = 0; i < _count; i++) {
        if (strcmp(_entries[i].key, key) == 0) {
            return _entries[i].value;
        }
    }

    // Fall back to English defaults
    for (size_t i = 0; DEFAULT_STRINGS[i].key != nullptr; i++) {
        if (strcmp(DEFAULT_STRINGS[i].key, key) == 0) {
            return DEFAULT_STRINGS[i].en;
        }
    }

    // Key not found anywhere — return key itself
    return key;
}

}  // namespace mclite
