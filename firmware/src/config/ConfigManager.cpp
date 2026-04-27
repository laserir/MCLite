#include "ConfigManager.h"
#include "../storage/SDCard.h"
#include "defaults.h"
#include <Arduino.h>
#include <mbedtls/sha256.h>

namespace mclite {

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
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
    _config.display.brightness    = defaults::DISPLAY_BRIGHTNESS;
    _config.display.autoDimSeconds = defaults::AUTO_DIM_SECONDS;
    _config.display.theme         = "dark";
    _config.display.dimBrightness = defaults::DIM_BRIGHTNESS;
    _config.display.kbdBacklight  = defaults::KBD_BACKLIGHT;
    _config.display.kbdBrightness = defaults::KBD_BRIGHTNESS;
    _config.messaging.saveHistory      = defaults::SAVE_HISTORY;
    _config.messaging.maxHistoryPerChat = defaults::MAX_HISTORY_PER_CHAT;
    _config.messaging.locationFormat   = defaults::LOCATION_FORMAT;
    _config.messaging.maxRetries       = defaults::MAX_RETRIES;
    _config.messaging.requestTelemetry = defaults::REQUEST_TELEMETRY;
    _config.messaging.showTelemetry    = defaults::SHOW_TELEMETRY;
    _config.messaging.cannedMessages   = defaults::CANNED_MESSAGES_ENABLED;
    _config.messaging.cannedCustom.clear();
    _config.soundEnabled = defaults::SOUND_ENABLED;
    _config.sosKeyword   = defaults::SOS_KEYWORD;
    _config.sosRepeat    = defaults::SOS_REPEAT;
    _config.gpsEnabled     = defaults::GPS_ENABLED;
    _config.gpsClockOffset = defaults::GPS_CLOCK_OFFSET;
    _config.gpsTimezone    = defaults::GPS_TIMEZONE;
    _config.gpsLastKnownMaxAge = defaults::GPS_LAST_KNOWN_MAX_AGE;
    _config.battery.lowAlertEnabled   = defaults::BATTERY_LOW_ALERT_ENABLED;
    _config.battery.lowAlertThreshold = defaults::BATTERY_LOW_ALERT_THRESHOLD;
    _config.security.lockMode     = defaults::LOCK_MODE;
    _config.security.pinCode      = defaults::PIN_CODE;
    _config.security.autoLock     = defaults::AUTO_LOCK;
    _config.security.adminEnabled = defaults::ADMIN_ENABLED;
    _config.language = defaults::LANGUAGE;
}

bool ConfigManager::parseJson(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[Config] Parse error: %s\n", err.c_str());
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
                Serial.printf("[Config] Channel '%s' missing 'index' field, defaulting to 0\n", cc.name.c_str());
            cc.index    = ch["index"] | 0;
            cc.allowSos = ch["allow_sos"] | true;
            cc.sendSos  = ch["send_sos"] | true;
            cc.readOnly = ch["read_only"] | false;
            cc.scope    = ch["scope"] | "";
            // Private channels require a PSK; hashtag channels can derive from name
            if (cc.psk.length() > 0 || cc.type == "hashtag") {
                // Skip channels with duplicate indices
                bool dupe = false;
                for (const auto& existing : _config.channels) {
                    if (existing.index == cc.index) {
                        Serial.printf("[Config] Skipping channel '%s' — duplicate index %d\n",
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

    // Offgrid — missing block defaults to enabled=false (backwards compat)
    _config.offgrid.enabled = doc["offgrid"]["enabled"] | false;

    Serial.printf("[Config] Loaded: device=%s, contacts=%d, channels=%d\n",
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
    }

    JsonArray rooms = doc["room_servers"].to<JsonArray>();
    for (const auto& r : _config.roomServers) {
        JsonObject obj = rooms.add<JsonObject>();
        obj["name"]       = r.name;
        obj["public_key"] = r.publicKey;
        obj["password"]   = r.password;
        obj["allow_sos"]  = r.allowSos;
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

    JsonObject msg = doc["messaging"].to<JsonObject>();
    msg["save_history"]         = _config.messaging.saveHistory;
    msg["max_history_per_chat"] = _config.messaging.maxHistoryPerChat;
    msg["location_format"]      = _config.messaging.locationFormat;
    msg["max_retries"]          = _config.messaging.maxRetries;
    msg["request_telemetry"]    = _config.messaging.requestTelemetry;
    msg["show_telemetry"]       = _config.messaging.showTelemetry;
    msg["canned_messages"]      = _config.messaging.cannedMessages;

    doc["sound"]["enabled"]     = _config.soundEnabled;
    doc["sound"]["sos_keyword"] = _config.sosKeyword;
    doc["sound"]["sos_repeat"]  = _config.sosRepeat;
    doc["gps"]["enabled"]            = _config.gpsEnabled;
    doc["gps"]["clock_offset"]       = _config.gpsClockOffset;
    if (_config.gpsTimezone.length() > 0) {
        doc["gps"]["timezone"]       = _config.gpsTimezone;
    }
    doc["gps"]["last_known_max_age"] = _config.gpsLastKnownMaxAge;

    doc["battery"]["low_alert_enabled"]   = _config.battery.lowAlertEnabled;
    doc["battery"]["low_alert_threshold"] = _config.battery.lowAlertThreshold;

    doc["security"]["lock"]           = _config.security.lockMode;
    doc["security"]["pin_code"]      = _config.security.pinCode;
    doc["security"]["auto_lock"]     = _config.security.autoLock;
    doc["security"]["admin_enabled"] = _config.security.adminEnabled;

    doc["offgrid"]["enabled"] = _config.offgrid.enabled;

    String output;
    serializeJsonPretty(doc, output);
    return output;
}

ConfigManager::LoadResult ConfigManager::load() {
    applyDefaults();

    auto& sd = SDCard::instance();
    if (!sd.isMounted()) {
        Serial.println("[Config] SD not mounted, using defaults");
        return LOAD_NO_FILE;
    }

    if (!sd.fileExists(defaults::CONFIG_PATH)) {
        Serial.println("[Config] config.json not found");
        return LOAD_NO_FILE;
    }

    String json = sd.readFile(defaults::CONFIG_PATH);
    if (json.isEmpty()) {
        Serial.println("[Config] Empty config file");
        return LOAD_ERROR;
    }

    if (!parseJson(json)) {
        return LOAD_ERROR;
    }
    return LOAD_OK;
}

bool ConfigManager::save() {
    auto& sd = SDCard::instance();
    if (!sd.isMounted()) return false;
    String json = toJson();
    return sd.writeFile(defaults::CONFIG_PATH, json);
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
    mc.sendSos = true;
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

}  // namespace mclite
