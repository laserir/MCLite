#include "ConfigManager.h"
#include "util/log.h"
#include "../storage/SDCard.h"
#include "defaults.h"
#include <Arduino.h>
#include <mbedtls/sha256.h>

namespace mclite {

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

// Per-conversation quick replies: parse an optional "canned" string array
// (cap 8, skip blanks) shared by contacts / channels / room_servers.
static void parseCannedArray(JsonObject obj, std::vector<String>& out) {
    JsonArray arr = obj["canned"];
    if (!arr) return;
    for (size_t i = 0; i < arr.size() && out.size() < 8; i++) {
        String s = arr[i].as<String>();
        if (s.length() > 0) out.push_back(s);
    }
}

// Serialize the "canned" array only when non-empty (mirrors from_discovery's
// conditional emit — keeps config.json clean for the common global-list case).
static void serializeCannedArray(JsonObject obj, const std::vector<String>& canned) {
    if (canned.empty()) return;
    JsonArray arr = obj["canned"].to<JsonArray>();
    for (size_t i = 0; i < canned.size() && i < 8; i++) {
        if (canned[i].length() > 0) arr.add(canned[i]);
    }
}

void ConfigManager::applyDefaults() {
    _config.deviceName = defaults::DEVICE_NAME;
    _config.radio.frequency       = defaults::RADIO_FREQUENCY;
    _config.radio.spreadingFactor = defaults::RADIO_SPREADING_FACTOR;
    _config.radio.bandwidth       = defaults::RADIO_BANDWIDTH;
    _config.radio.txPower         = defaults::RADIO_TX_POWER;
    _config.radio.codingRate      = defaults::RADIO_CODING_RATE;
    _config.radio.scope           = defaults::RADIO_SCOPE;
    _config.radio.pathHashMode    = defaults::RADIO_PATH_HASH_MODE;
    _config.radio.advertIntervalMin = defaults::RADIO_ADVERT_INTERVAL_MIN;
    _config.display.brightness    = defaults::DISPLAY_BRIGHTNESS;
    _config.display.autoDimSeconds = defaults::AUTO_DIM_SECONDS;
    _config.display.theme         = "dark";
    _config.display.dimBrightness = defaults::DIM_BRIGHTNESS;
    _config.display.kbdBacklight  = defaults::KBD_BACKLIGHT;
    _config.display.kbdBrightness = defaults::KBD_BRIGHTNESS;
    _config.display.emoji         = defaults::EMOJI_ENABLED;
    _config.messaging.saveHistory      = defaults::SAVE_HISTORY;
    _config.messaging.maxHistoryPerChat = defaults::MAX_HISTORY_PER_CHAT;
    _config.messaging.locationFormat   = defaults::LOCATION_FORMAT;
    _config.messaging.maxRetries       = defaults::MAX_RETRIES;
    _config.messaging.requestTelemetry = defaults::REQUEST_TELEMETRY;
    _config.messaging.showTelemetry    = defaults::SHOW_TELEMETRY;
    _config.messaging.cannedMessages   = defaults::CANNED_MESSAGES_ENABLED;
    _config.messaging.cannedCustom.clear();
    _config.messaging.allowMute        = defaults::ALLOW_MUTE;
    _config.messaging.autoTelemetry    = defaults::AUTO_TELEMETRY;
    _config.messaging.shareContact     = defaults::SHARE_CONTACT;
    _config.soundEnabled = defaults::SOUND_ENABLED;
    _config.sosKeyword   = defaults::SOS_KEYWORD;
    _config.sosRepeat    = defaults::SOS_REPEAT;
    _config.gpsEnabled     = defaults::GPS_ENABLED;
    _config.gpsClockOffset = defaults::GPS_CLOCK_OFFSET;
    _config.gpsTimezone    = defaults::GPS_TIMEZONE;
    _config.gpsLastKnownMaxAge = defaults::GPS_LAST_KNOWN_MAX_AGE;
    _config.locationPrecision = defaults::GPS_LOCATION_PRECISION;
    _config.battery.lowAlertEnabled   = defaults::BATTERY_LOW_ALERT_ENABLED;
    _config.battery.lowAlertThreshold = defaults::BATTERY_LOW_ALERT_THRESHOLD;
    _config.security.lockMode     = defaults::LOCK_MODE;
    _config.security.pinCode      = defaults::PIN_CODE;
    _config.security.autoLock     = defaults::AUTO_LOCK;
    _config.security.adminEnabled = defaults::ADMIN_ENABLED;
    _config.permissions.settings              = defaults::PERM_SETTINGS;
    _config.permissions.conversationManagement = defaults::PERM_CONVERSATION_MGMT;
    _config.permissions.companion             = defaults::PERM_COMPANION;
    _config.debug.screenshots     = defaults::SCREENSHOTS_ENABLED;
    _config.language = defaults::LANGUAGE;
}

bool ConfigManager::parseJson(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        LOGF("[Config] Parse error: %s\n", err.c_str());
        return false;
    }

    // Language
    _config.language = doc["language"] | defaults::LANGUAGE;

    // Device (MeshCore advert payload is 32 bytes; name shares space with GPS/flags)
    _config.deviceName = doc["device"]["name"] | defaults::DEVICE_NAME;
    if (_config.deviceName.length() > 20) _config.deviceName = _config.deviceName.substring(0, 20);

    // Radio
    JsonObject radio = doc["radio"];
    if (radio) {
        _config.radio.frequency       = radio["frequency"] | defaults::RADIO_FREQUENCY;
        _config.radio.spreadingFactor = radio["spreading_factor"] | defaults::RADIO_SPREADING_FACTOR;
        _config.radio.bandwidth       = radio["bandwidth"] | defaults::RADIO_BANDWIDTH;
        _config.radio.txPower         = radio["tx_power"] | defaults::RADIO_TX_POWER;
        _config.radio.codingRate      = radio["coding_rate"] | defaults::RADIO_CODING_RATE;

        // Bounds check — out of range falls back to defaults
        if (_config.radio.frequency < 150.0f || _config.radio.frequency > 960.0f)
            _config.radio.frequency = defaults::RADIO_FREQUENCY;
        if (_config.radio.spreadingFactor < 5 || _config.radio.spreadingFactor > 12)
            _config.radio.spreadingFactor = defaults::RADIO_SPREADING_FACTOR;
        if (_config.radio.bandwidth < 7.8f || _config.radio.bandwidth > 500.0f)
            _config.radio.bandwidth = defaults::RADIO_BANDWIDTH;
        if (_config.radio.txPower < -9 || _config.radio.txPower > 22)
            _config.radio.txPower = defaults::RADIO_TX_POWER;
        if (_config.radio.codingRate < 5 || _config.radio.codingRate > 8)
            _config.radio.codingRate = defaults::RADIO_CODING_RATE;
        _config.radio.scope           = radio["scope"] | defaults::RADIO_SCOPE;
        _config.radio.pathHashMode    = radio["path_hash_mode"] | defaults::RADIO_PATH_HASH_MODE;
        if (_config.radio.pathHashMode > 2)
            _config.radio.pathHashMode = defaults::RADIO_PATH_HASH_MODE;

        // Periodic flood-advert interval. 0 = off (default). When enabled, enforce a
        // 1-hour floor (never more frequent than hourly) up to a 1-week cap — periodic
        // flood adverts spam shared meshes (issue #13).
        _config.radio.advertIntervalMin = radio["advert_interval_min"] | defaults::RADIO_ADVERT_INTERVAL_MIN;
        if (_config.radio.advertIntervalMin != 0) {
            if (_config.radio.advertIntervalMin < 60)    _config.radio.advertIntervalMin = 60;
            if (_config.radio.advertIntervalMin > 10080) _config.radio.advertIntervalMin = 10080;
        }
    }

    // Identity
    _config.privateKey = doc["identity"]["private_key"] | "";
    _config.publicKey  = doc["identity"]["public_key"]  | "";

    // Contacts
    _config.contacts.clear();
    JsonArray contacts = doc["contacts"];
    if (contacts) {
        for (JsonObject c : contacts) {
            ContactConfig cc;
            cc.alias          = c["alias"] | "Unknown";
            cc.publicKey      = c["public_key"] | "";
            cc.allowTelemetry = c["allow_telemetry"] | true;
            cc.allowLocation  = c["allow_location"]  | false;
            cc.allowEnvironment = c["allow_environment"] | false;
            cc.alwaysSound    = c["always_sound"]     | false;
            cc.allowSos       = c["allow_sos"]        | true;
            cc.sendSos        = c["send_sos"]         | true;
            cc.fromDiscovery  = c["from_discovery"]   | false;
            parseCannedArray(c, cc.canned);
            if (cc.publicKey.length() > 0) {
                _config.contacts.push_back(cc);
            }
        }
    }

    // Room servers
    _config.roomServers.clear();
    JsonArray rooms = doc["room_servers"];
    if (rooms) {
        for (JsonObject r : rooms) {
            RoomServerConfig rc;
            rc.name      = r["name"] | "Room";
            rc.publicKey = r["public_key"] | "";
            rc.password  = r["password"]   | "";
            rc.allowSos  = r["allow_sos"]  | true;
            rc.sendSos   = r["send_sos"]   | false;
            rc.readOnly  = r["read_only"]  | false;
            rc.scope     = r["scope"]      | "";
            parseCannedArray(r, rc.canned);
            // Normalize pubkey to lowercase. UIManager compares
            // publicKey.substring(0, 16) against ConvoId::id (always lowercase
            // from pubKeyToShortId), so hand-edited uppercase hex would
            // otherwise silently break outgoing paths (handleSend, retry,
            // chat-open re-login, SOS broadcast).
            rc.publicKey.toLowerCase();
            // Match MeshCore's BaseChatMesh::sendLogin truncation (BaseChatMesh.cpp:553)
            if (rc.password.length() > 15) {
                rc.password = rc.password.substring(0, 15);
            }
            // Skip rooms with empty pubkey (nothing to log in to)
            if (rc.publicKey.length() > 0) {
                _config.roomServers.push_back(rc);
            }
        }
    }

    // Channels
    _config.channels.clear();
    JsonArray channels = doc["channels"];
    if (channels) {
        for (JsonObject ch : channels) {
            ChannelConfig cc;
            cc.name     = ch["name"] | "Channel";
            cc.type     = ch["type"] | "hashtag";
            cc.psk      = ch["psk"] | "";
            if (ch["index"].isNull())
                LOGF("[Config] Channel '%s' missing 'index' field, defaulting to 0\n", cc.name.c_str());
            cc.index    = ch["index"] | 0;
            cc.allowSos = ch["allow_sos"] | true;
            // Default send_sos: true only for private channels (trusted, small group);
            // false for public/hashtag (avoid spamming community channels). Matches
            // room-server default. Explicit field always wins.
            bool defaultSendSos = (cc.type == "private");
            cc.sendSos  = ch["send_sos"] | defaultSendSos;
            cc.readOnly = ch["read_only"] | false;
            cc.scope    = ch["scope"] | "";
            parseCannedArray(ch, cc.canned);
            // Private channels require a PSK; hashtag channels can derive from name
            if (cc.psk.length() > 0 || cc.type == "hashtag") {
                // Skip channels with duplicate indices
                bool dupe = false;
                for (const auto& existing : _config.channels) {
                    if (existing.index == cc.index) {
                        LOGF("[Config] Skipping channel '%s' — duplicate index %d\n",
                                      cc.name.c_str(), cc.index);
                        dupe = true;
                        break;
                    }
                }
                if (!dupe) _config.channels.push_back(cc);
            }
        }
    }

    // Display
    JsonObject disp = doc["display"];
    if (disp) {
        _config.display.brightness     = disp["brightness"] | defaults::DISPLAY_BRIGHTNESS;
        _config.display.autoDimSeconds = disp["auto_dim_seconds"] | defaults::AUTO_DIM_SECONDS;
        _config.display.theme          = disp["theme"] | "dark";
        _config.display.bootText       = disp["boot_text"] | defaults::BOOT_TEXT;
        _config.display.dimBrightness  = disp["dim_brightness"] | defaults::DIM_BRIGHTNESS;
        _config.display.kbdBacklight   = disp["kbd_backlight"] | defaults::KBD_BACKLIGHT;
        uint8_t kbdBr = disp["kbd_brightness"] | defaults::KBD_BRIGHTNESS;
        _config.display.kbdBrightness  = constrain(kbdBr, 1, 255);
        _config.display.emoji          = disp["emoji"] | defaults::EMOJI_ENABLED;

        // Custom themes (display.themes[]) — each is {name, base?, <colorKey>:"#RRGGBB"…}.
        // Stored verbatim as strings; the theme layer validates/resolves them at boot.
        _config.display.customThemes.clear();
        JsonArray themesArr = disp["themes"];
        if (!themesArr.isNull()) {
            for (JsonObject t : themesArr) {
                String name = t["name"] | "";
                if (name.length() == 0) continue;
                CustomTheme ct;
                ct.name = name;
                ct.base = t["base"] | "dark";
                for (JsonPair kv : t) {
                    String k = kv.key().c_str();
                    if (k == "name" || k == "base") continue;
                    if (kv.value().is<const char*>()) {
                        ct.colors.push_back({k, String(kv.value().as<const char*>())});
                    }
                }
                _config.display.customThemes.push_back(ct);
            }
        }
    }

    // Messaging
    JsonObject msg = doc["messaging"];
    if (msg) {
        _config.messaging.saveHistory      = msg["save_history"] | defaults::SAVE_HISTORY;
        _config.messaging.maxHistoryPerChat = msg["max_history_per_chat"] | defaults::MAX_HISTORY_PER_CHAT;
        _config.messaging.locationFormat   = msg["location_format"] | defaults::LOCATION_FORMAT;
        uint8_t retries = msg["max_retries"] | defaults::MAX_RETRIES;
        _config.messaging.maxRetries       = constrain(retries, 1, 5);
        _config.messaging.requestTelemetry = msg["request_telemetry"] | defaults::REQUEST_TELEMETRY;
        String showTelem = msg["show_telemetry"] | defaults::SHOW_TELEMETRY;
        if (showTelem == "battery" || showTelem == "location" || showTelem == "both" || showTelem == "none") {
            _config.messaging.showTelemetry = showTelem;
        } else {
            _config.messaging.showTelemetry = defaults::SHOW_TELEMETRY;
        }

        // Canned messages: bool = toggle, array = custom messages (implies enabled)
        JsonVariant cm = msg["canned_messages"];
        if (cm.is<bool>()) {
            _config.messaging.cannedMessages = cm.as<bool>();
        } else if (cm.is<JsonArray>()) {
            _config.messaging.cannedMessages = true;
            JsonArray arr = cm.as<JsonArray>();
            for (size_t i = 0; i < arr.size() && i < 8; i++) {
                _config.messaging.cannedCustom.push_back(arr[i].as<String>());
            }
        } else {
            _config.messaging.cannedMessages = defaults::CANNED_MESSAGES_ENABLED;
        }

        _config.messaging.allowMute = msg["allow_mute"] | defaults::ALLOW_MUTE;
        _config.messaging.autoTelemetry = msg["auto_telemetry"] | defaults::AUTO_TELEMETRY;
        _config.messaging.shareContact = msg["share_contact"] | defaults::SHARE_CONTACT;
    }

    // Sound
    _config.soundEnabled = doc["sound"]["enabled"] | defaults::SOUND_ENABLED;
    _config.sosKeyword   = doc["sound"]["sos_keyword"] | defaults::SOS_KEYWORD;
    uint8_t sosRepeat    = doc["sound"]["sos_repeat"]  | defaults::SOS_REPEAT;
    _config.sosRepeat    = constrain(sosRepeat, 1, 10);

    // GPS
    _config.gpsEnabled = doc["gps"]["enabled"] | defaults::GPS_ENABLED;
    int8_t clockOff = doc["gps"]["clock_offset"] | defaults::GPS_CLOCK_OFFSET;
    _config.gpsClockOffset = constrain(clockOff, -12, 14);
    _config.gpsTimezone = doc["gps"]["timezone"] | defaults::GPS_TIMEZONE;
    uint16_t lastKnownAge = doc["gps"]["last_known_max_age"] | defaults::GPS_LAST_KNOWN_MAX_AGE;
    _config.gpsLastKnownMaxAge = constrain(lastKnownAge, (uint16_t)60, (uint16_t)7200);
    // Location-advert precision. Prefer the new key; fall back to the legacy
    // `location_advert` bool (true → exact / 32, false → off / 0). 0 = off;
    // otherwise clamp to [10, 32] (never finer than full, never an absurd shift).
    uint8_t locPrec;
    if (doc["gps"]["location_precision"].is<int>()) {
        locPrec = (uint8_t)constrain((int)(doc["gps"]["location_precision"] | 0), 0, 32);
        if (locPrec != 0 && locPrec < 10) locPrec = 10;
    } else {
        locPrec = (doc["gps"]["location_advert"] | false) ? 32 : 0;
    }
    _config.locationPrecision = locPrec;

    // Battery
    _config.battery.lowAlertEnabled   = doc["battery"]["low_alert_enabled"] | defaults::BATTERY_LOW_ALERT_ENABLED;
    uint8_t threshold = doc["battery"]["low_alert_threshold"] | defaults::BATTERY_LOW_ALERT_THRESHOLD;
    _config.battery.lowAlertThreshold = constrain(threshold, 5, 50);

    // Security — new unified lock model with backwards compat for old booleans
    if (doc["security"]["lock"].is<const char*>()) {
        String mode = doc["security"]["lock"] | defaults::LOCK_MODE;
        if (mode != "none" && mode != "key" && mode != "pin") mode = "none";
        _config.security.lockMode = mode;
    } else {
        // Backwards compat: old pin_enabled / key_lock booleans
        bool pinEnabled = doc["security"]["pin_enabled"] | false;
        bool keyLock    = doc["security"]["key_lock"] | true;
        if (pinEnabled)       _config.security.lockMode = "pin";
        else if (keyLock)     _config.security.lockMode = "key";
        else                  _config.security.lockMode = "none";
    }
    _config.security.pinCode      = doc["security"]["pin_code"] | defaults::PIN_CODE;
    _config.security.adminEnabled = doc["security"]["admin_enabled"] | defaults::ADMIN_ENABLED;

    if (doc["security"]["auto_lock"].is<const char*>()) {
        String mode = doc["security"]["auto_lock"] | defaults::AUTO_LOCK;
        if (mode != "none" && mode != "key" && mode != "pin") mode = "none";
        _config.security.autoLock = mode;
    } else {
        // Backwards compat: old format
        bool pinEnabled  = doc["security"]["pin_enabled"] | false;
        if (pinEnabled) {
            // Old firmware always auto-locked to PIN on dim when pin_enabled
            _config.security.autoLock = "pin";
        } else if (doc["security"]["auto_key_lock"].is<bool>()) {
            // Explicit auto_key_lock field — respect it
            _config.security.autoLock = doc["security"]["auto_key_lock"].as<bool>() ? "key" : "none";
        }
        // else: field missing, not pin — keep default (AUTO_LOCK = "key")
    }

    // Permissions — within the Admin gate. Missing block keeps the permissive
    // defaults (full settings, companion shown) so existing configs are unchanged.
    {
        String s = doc["permissions"]["settings"] | defaults::PERM_SETTINGS;
        if (s != "full" && s != "restricted" && s != "none") s = defaults::PERM_SETTINGS;
        _config.permissions.settings = s;
        _config.permissions.conversationManagement =
            doc["permissions"]["conversation_management"] | defaults::PERM_CONVERSATION_MGMT;
        _config.permissions.companion = doc["permissions"]["companion"] | defaults::PERM_COMPANION;
    }

    // Offgrid — missing block defaults to enabled=false (backwards compat)
    _config.offgrid.enabled = doc["offgrid"]["enabled"] | false;

    // WiFi — missing block defaults to empty (disabled). Used for firmware auto-update.
    _config.wifi.ssid       = doc["wifi"]["ssid"]        | "";
    _config.wifi.password   = doc["wifi"]["password"]    | "";
    _config.wifi.autoUpdate = doc["wifi"]["auto_update"] | false;

    _config.ble.pin = doc["ble"]["pin"] | 0u;   // 0 = auto-generate on first BLE use

    // Debug — missing block defaults to all-off
    _config.debug.screenshots = doc["debug"]["screenshots"] | defaults::SCREENSHOTS_ENABLED;

    LOGF("[Config] Loaded: device=%s, contacts=%d, channels=%d\n",
                  _config.deviceName.c_str(),
                  _config.contacts.size(),
                  _config.channels.size());
    return true;
}

String ConfigManager::toJson() const {
    JsonDocument doc;

    if (_config.language.length() > 0) {
        doc["language"] = _config.language;
    }

    doc["device"]["name"] = _config.deviceName;

    JsonObject radio = doc["radio"].to<JsonObject>();
    radio["frequency"]        = _config.radio.frequency;
    radio["spreading_factor"] = _config.radio.spreadingFactor;
    radio["bandwidth"]        = _config.radio.bandwidth;
    radio["tx_power"]         = _config.radio.txPower;
    radio["coding_rate"]      = _config.radio.codingRate;
    if (_config.radio.scope != "*") {
        radio["scope"]        = _config.radio.scope;
    }
    if (_config.radio.pathHashMode != defaults::RADIO_PATH_HASH_MODE) {
        radio["path_hash_mode"] = _config.radio.pathHashMode;
    }
    if (_config.radio.advertIntervalMin != 0) {
        radio["advert_interval_min"] = _config.radio.advertIntervalMin;
    }

    doc["identity"]["private_key"] = _config.privateKey;
    doc["identity"]["public_key"]  = _config.publicKey;

    JsonArray contacts = doc["contacts"].to<JsonArray>();
    for (const auto& c : _config.contacts) {
        JsonObject obj = contacts.add<JsonObject>();
        obj["alias"]           = c.alias;
        obj["public_key"]      = c.publicKey;
        obj["allow_telemetry"] = c.allowTelemetry;
        obj["allow_location"]  = c.allowLocation;
        obj["allow_environment"] = c.allowEnvironment;
        obj["always_sound"]    = c.alwaysSound;
        obj["allow_sos"]       = c.allowSos;
        obj["send_sos"]        = c.sendSos;
        obj["from_discovery"]  = c.fromDiscovery;
        serializeCannedArray(obj, c.canned);
    }

    JsonArray rooms = doc["room_servers"].to<JsonArray>();
    for (const auto& r : _config.roomServers) {
        JsonObject obj = rooms.add<JsonObject>();
        obj["name"]       = r.name;
        obj["public_key"] = r.publicKey;
        obj["password"]   = r.password;
        obj["allow_sos"]  = r.allowSos;
        obj["send_sos"]   = r.sendSos;
        if (r.readOnly) obj["read_only"] = true;
        if (r.scope.length() > 0) obj["scope"] = r.scope;
        serializeCannedArray(obj, r.canned);
    }

    JsonArray channels = doc["channels"].to<JsonArray>();
    for (const auto& ch : _config.channels) {
        JsonObject obj = channels.add<JsonObject>();
        obj["name"]  = ch.name;
        obj["type"]  = ch.type;
        obj["psk"]   = ch.psk;
        obj["index"]    = ch.index;
        obj["allow_sos"] = ch.allowSos;
        obj["send_sos"] = ch.sendSos;
        if (ch.readOnly) {
            obj["read_only"] = true;
        }
        if (ch.scope.length() > 0) {
            obj["scope"] = ch.scope;
        }
        serializeCannedArray(obj, ch.canned);
    }

    JsonObject disp = doc["display"].to<JsonObject>();
    disp["brightness"]       = _config.display.brightness;
    disp["auto_dim_seconds"] = _config.display.autoDimSeconds;
    disp["theme"]            = _config.display.theme;
    if (_config.display.bootText.length() > 0) {
        disp["boot_text"]    = _config.display.bootText;
    }
    disp["dim_brightness"]   = _config.display.dimBrightness;
    disp["kbd_backlight"]    = _config.display.kbdBacklight;
    disp["kbd_brightness"]   = _config.display.kbdBrightness;
    disp["emoji"]            = _config.display.emoji;
    if (!_config.display.customThemes.empty()) {
        JsonArray themesArr = disp["themes"].to<JsonArray>();
        for (const auto& ct : _config.display.customThemes) {
            JsonObject o = themesArr.add<JsonObject>();
            o["name"] = ct.name;
            o["base"] = ct.base;
            for (const auto& c : ct.colors) o[c.first] = c.second;
        }
    }

    JsonObject msg = doc["messaging"].to<JsonObject>();
    msg["save_history"]         = _config.messaging.saveHistory;
    msg["max_history_per_chat"] = _config.messaging.maxHistoryPerChat;
    msg["location_format"]      = _config.messaging.locationFormat;
    msg["max_retries"]          = _config.messaging.maxRetries;
    msg["request_telemetry"]    = _config.messaging.requestTelemetry;
    msg["show_telemetry"]       = _config.messaging.showTelemetry;
    if (_config.messaging.cannedCustom.empty()) {
        msg["canned_messages"] = _config.messaging.cannedMessages;
    } else {
        JsonArray cannedArr = msg["canned_messages"].to<JsonArray>();
        for (size_t i = 0; i < _config.messaging.cannedCustom.size() && i < 8; i++) {
            if (_config.messaging.cannedCustom[i].length() > 0)
                cannedArr.add(_config.messaging.cannedCustom[i]);
        }
    }
    msg["allow_mute"]           = _config.messaging.allowMute;
    msg["auto_telemetry"]       = _config.messaging.autoTelemetry;
    msg["share_contact"]        = _config.messaging.shareContact;

    doc["sound"]["enabled"]     = _config.soundEnabled;
    doc["sound"]["sos_keyword"] = _config.sosKeyword;
    doc["sound"]["sos_repeat"]  = _config.sosRepeat;
    doc["gps"]["enabled"]            = _config.gpsEnabled;
    doc["gps"]["clock_offset"]       = _config.gpsClockOffset;
    if (_config.gpsTimezone.length() > 0) {
        doc["gps"]["timezone"]       = _config.gpsTimezone;
    }
    doc["gps"]["last_known_max_age"] = _config.gpsLastKnownMaxAge;
    doc["gps"]["location_precision"] = _config.locationPrecision;

    doc["battery"]["low_alert_enabled"]   = _config.battery.lowAlertEnabled;
    doc["battery"]["low_alert_threshold"] = _config.battery.lowAlertThreshold;

    doc["security"]["lock"]           = _config.security.lockMode;
    doc["security"]["pin_code"]      = _config.security.pinCode;
    doc["security"]["auto_lock"]     = _config.security.autoLock;
    doc["security"]["admin_enabled"] = _config.security.adminEnabled;

    doc["permissions"]["settings"]                = _config.permissions.settings;
    doc["permissions"]["conversation_management"] = _config.permissions.conversationManagement;
    doc["permissions"]["companion"]               = _config.permissions.companion;

    doc["offgrid"]["enabled"] = _config.offgrid.enabled;

    // WiFi — only emit when an SSID is set, to keep config.json clean on devices that don't use it.
    if (_config.wifi.ssid.length() > 0) {
        doc["wifi"]["ssid"]        = _config.wifi.ssid;
        doc["wifi"]["password"]    = _config.wifi.password;
        doc["wifi"]["auto_update"] = _config.wifi.autoUpdate;
    }

    // BLE — emit the persisted pairing PIN once one has been generated.
    if (_config.ble.pin != 0) {
        doc["ble"]["pin"] = _config.ble.pin;
    }

    // Debug — only emit when something is enabled (keeps the file clean by default).
    if (_config.debug.screenshots) {
        doc["debug"]["screenshots"] = _config.debug.screenshots;
    }

    String output;
    serializeJsonPretty(doc, output);
    return output;
}

ConfigManager::LoadResult ConfigManager::load() {
    applyDefaults();

    auto& sd = SDCard::instance();
    if (!sd.isMounted()) {
        LOGLN("[Config] SD not mounted, using defaults");
        return LOAD_NO_FILE;
    }

    String bakPath = String(defaults::CONFIG_PATH) + ".bak";
    bool mainExists = sd.fileExists(defaults::CONFIG_PATH);
    bool bakExists  = sd.fileExists(bakPath.c_str());

    if (!mainExists && !bakExists) {
        LOGLN("[Config] config.json not found");
        return LOAD_NO_FILE;
    }

    // Try the live file first.
    if (mainExists) {
        String json = sd.readFile(defaults::CONFIG_PATH);
        if (!json.isEmpty() && parseJson(json)) {
            return LOAD_OK;
        }
        LOGLN("[Config] config.json corrupt or empty");
    }

    // Fall back to the .bak left behind by writeAtomic.
    if (bakExists) {
        LOGLN("[Config] Trying config.json.bak");
        String json = sd.readFile(bakPath.c_str());
        if (!json.isEmpty() && parseJson(json)) {
            LOGLN("[Config] Loaded from config.json.bak");
            // Promote the recovery content into main, then drop .bak.
            // Without this step, the next save's writeAtomic would rotate
            // the existing (corrupt or missing) main into .bak, destroying
            // the only good copy. The writeFile is non-atomic, but if it
            // fails or is interrupted, .bak is still intact and the next
            // boot will simply re-recover (idempotent — same content).
            if (sd.writeFile(defaults::CONFIG_PATH, json)) {
                sd.remove(bakPath.c_str());
                LOGLN("[Config] Recovery promoted; .bak removed");
            } else {
                LOGLN("[Config] WARN: could not promote recovery to main; .bak retained");
            }
            return LOAD_OK;
        }
        LOGLN("[Config] config.json.bak also unusable");
    }

    return LOAD_ERROR;
}

bool ConfigManager::appendDiscoveredContact(const ContactConfig& cc) {
    if ((int)_config.contacts.size() >= defaults::MAX_CHAT_CONTACTS) {
        LOGF("[Config] appendDiscoveredContact: at cap %d/%d\n",
                      (int)_config.contacts.size(), defaults::MAX_CHAT_CONTACTS);
        return false;
    }
    for (const auto& existing : _config.contacts) {
        if (existing.publicKey == cc.publicKey) {
            LOGF("[Config] appendDiscoveredContact: duplicate alias=%s\n",
                          cc.alias.c_str());
            return false;
        }
    }
    _config.contacts.push_back(cc);
    if (!save()) {
        // Roll back the in-memory append so a retry isn't blocked as duplicate.
        _config.contacts.pop_back();
        LOGLN("[Config] appendDiscoveredContact: save failed");
        return false;
    }
    LOGF("[Config] Saved discovered contact: %s\n", cc.alias.c_str());
    return true;
}

// Local helper: true if s is exactly `len` hex digits.
static bool isHexLen(const String& s, size_t len) {
    if (s.length() != len) return false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

bool ConfigManager::hasPublicChannel() const {
    for (const auto& ch : _config.channels) if (ch.type == "public") return true;
    return false;
}

bool ConfigManager::appendChannel(const ChannelConfig& in) {
    if ((int)_config.channels.size() >= defaults::MAX_CHANNELS) {
        LOGF("[Config] appendChannel: at cap %d/%d\n",
                      (int)_config.channels.size(), defaults::MAX_CHANNELS);
        return false;
    }
    ChannelConfig cc = in;
    if (cc.type == "public") {
        if (hasPublicChannel()) { LOGLN("[Config] appendChannel: public already exists"); return false; }
        cc.name = "Public";
        cc.psk  = "8b3387e9c5cdea6ac9e5edbaa115cd72";
        cc.allowSos = false;
        cc.sendSos  = false;
    } else if (cc.type == "private") {
        // Private channels need an explicit PSK (16 or 32 raw bytes -> 32/64 hex).
        if (!(isHexLen(cc.psk, 32) || isHexLen(cc.psk, 64))) {
            LOGLN("[Config] appendChannel: invalid private PSK");
            return false;
        }
    } else {  // hashtag — PSK derived at boot from the (sanitized) name; psk stays empty
        if (cc.name.length() == 0) { LOGLN("[Config] appendChannel: empty name"); return false; }
    }
    for (const auto& ex : _config.channels) {
        if (ex.name.equalsIgnoreCase(cc.name)) {
            LOGF("[Config] appendChannel: duplicate name=%s\n", cc.name.c_str());
            return false;
        }
    }
    cc.index = (uint8_t)_config.channels.size();
    _config.channels.push_back(cc);
    if (!save()) {
        _config.channels.pop_back();
        LOGLN("[Config] appendChannel: save failed");
        return false;
    }
    LOGF("[Config] Saved channel: %s\n", cc.name.c_str());
    return true;
}

bool ConfigManager::appendRoom(const RoomServerConfig& in) {
    if ((int)_config.roomServers.size() >= defaults::MAX_ROOM_SERVERS) {
        LOGF("[Config] appendRoom: at cap %d/%d\n",
                      (int)_config.roomServers.size(), defaults::MAX_ROOM_SERVERS);
        return false;
    }
    RoomServerConfig rc = in;
    if (!isHexLen(rc.publicKey, 64)) { LOGLN("[Config] appendRoom: invalid pubkey"); return false; }
    rc.publicKey.toLowerCase();
    if (rc.password.length() > 15) rc.password = rc.password.substring(0, 15);
    for (const auto& ex : _config.roomServers) {
        if (ex.publicKey.equalsIgnoreCase(rc.publicKey)) {
            LOGF("[Config] appendRoom: duplicate pubkey for %s\n", rc.name.c_str());
            return false;
        }
    }
    _config.roomServers.push_back(rc);
    if (!save()) {
        _config.roomServers.pop_back();
        LOGLN("[Config] appendRoom: save failed");
        return false;
    }
    LOGF("[Config] Saved room: %s\n", rc.name.c_str());
    return true;
}

bool ConfigManager::removeContactAt(size_t i) {
    if (i >= _config.contacts.size()) return false;
    ContactConfig saved = _config.contacts[i];
    _config.contacts.erase(_config.contacts.begin() + i);
    if (!save()) {
        _config.contacts.insert(_config.contacts.begin() + i, saved);  // rollback
        return false;
    }
    return true;
}

bool ConfigManager::removeChannelAt(size_t i) {
    if (i >= _config.channels.size()) return false;
    std::vector<ChannelConfig> backup = _config.channels;
    _config.channels.erase(_config.channels.begin() + i);
    for (size_t k = 0; k < _config.channels.size(); k++) _config.channels[k].index = (uint8_t)k;
    if (!save()) {
        _config.channels = backup;  // rollback (restores order + indices)
        return false;
    }
    return true;
}

bool ConfigManager::removeRoomAt(size_t i) {
    if (i >= _config.roomServers.size()) return false;
    RoomServerConfig saved = _config.roomServers[i];
    _config.roomServers.erase(_config.roomServers.begin() + i);
    if (!save()) {
        _config.roomServers.insert(_config.roomServers.begin() + i, saved);  // rollback
        return false;
    }
    return true;
}

bool ConfigManager::save() {
    auto& sd = SDCard::instance();
    if (!sd.isMounted()) return false;
    String json = toJson();
    // config.json holds device identity keys — a torn truncate-write would
    // brick the unit. writeAtomic stages to tmp, keeps the prior file as
    // .bak, then renames into place. ConfigManager::load() falls back to
    // .bak if the live file is missing or unparseable.
    return sd.writeAtomic(defaults::CONFIG_PATH, json);
}

bool ConfigManager::generate() {
    applyDefaults();
    _config.channels.clear();
    // Identity generation will be handled by MeshManager
    // which uses MeshCore's Identity class
    _config.privateKey = "";
    _config.publicKey  = "";

    // Public channel (hardcoded MeshCore PSK)
    ChannelConfig pub;
    pub.name = "Public";
    pub.type = "public";
    pub.psk  = "8b3387e9c5cdea6ac9e5edbaa115cd72";
    pub.index = 0;
    pub.allowSos = false;
    pub.sendSos = false;
    pub.readOnly = false;
    _config.channels.push_back(pub);

    // Default hashtag channel — derive PSK as SHA256(name)[:16] → hex
    // Hashtag names must be lowercase (a-z, 0-9, -) per MeshCore convention
    ChannelConfig mc;
    mc.name = "#mclite";
    mc.type = "hashtag";
    mc.index = 1;
    mc.sendSos = false;  // Hashtag channels default to false (community-spam avoidance)
    mc.readOnly = false;
    {
        uint8_t hash[32];
        mbedtls_sha256((const uint8_t*)mc.name.c_str(), mc.name.length(), hash, 0);
        char hex[33];
        for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", hash[i]);
        hex[32] = '\0';
        mc.psk = String(hex);
    }
    _config.channels.push_back(mc);

    return save();
}

bool ConfigManager::hasIdentity() const {
    return _config.privateKey.length() > 0 && _config.publicKey.length() > 0;
}

bool ConfigManager::hasContacts() const {
    return !_config.contacts.empty() || !_config.channels.empty();
}

bool ConfigManager::hasContactByPubkeyHex(const String& hexKey) const {
    for (const auto& c : _config.contacts) {
        if (c.publicKey.equalsIgnoreCase(hexKey)) return true;
    }
    return false;
}

}  // namespace mclite
