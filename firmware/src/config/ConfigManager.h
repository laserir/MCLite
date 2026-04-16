#pragma once

#include <ArduinoJson.h>
#include <vector>
#include <cstdint>

namespace mclite {

struct ContactConfig {
    String alias;            // Display name (user-chosen)
    String publicKey;        // base64
    bool   allowTelemetry = true;   // Base telemetry permission (must be true for location/environment to work)
    bool   allowLocation  = false;  // Respond to GPS location requests (requires allow_telemetry)
    bool   allowEnvironment = false;  // Respond to environment sensor requests (requires allow_telemetry)
    bool   alwaysSound    = false;  // Play notification even when muted
    bool   allowSos       = true;   // Allow SOS alerts from this contact
    bool   sendSos        = true;   // Include in outgoing SOS broadcast
};

struct ChannelConfig {
    String  name;
    String  type;      // "hashtag" or "private"
    String  psk;       // hex-encoded pre-shared key
    uint8_t index;
    bool    allowSos = true;  // Allow SOS alerts from this channel
    bool    sendSos = true;   // Include in outgoing SOS broadcast
    bool    readOnly = false;  // Hide input bar in chat view
    String  scope;             // Region scope override ("" = inherit global, "*" = wildcard, "#name" = region)
};

struct RadioConfig {
    float   frequency       = 869.618f;
    uint8_t spreadingFactor = 8;
    float   bandwidth       = 62.5f;
    int8_t  txPower         = 22;
    uint8_t codingRate      = 8;
    String  scope           = "*";  // Region scope ("*" = no transport codes, "#name" = region)
};

struct DisplayConfig {
    uint8_t  brightness     = 180;
    uint16_t autoDimSeconds = 30;
    String   theme          = "dark";
    String   bootText       = "";   // Optional text shown on boot screen below version
    uint8_t  dimBrightness  = 20;    // Brightness when dimmed (0 = screen off)
    bool     kbdBacklight   = true;  // Keyboard backlight follows auto-dim (on/off)
    uint8_t  kbdBrightness  = 127;   // Keyboard backlight level (1-255)
};

struct MessagingConfig {
    bool     saveHistory      = true;
    uint16_t maxHistoryPerChat = 100;
    String   locationFormat   = "decimal";
    uint8_t  maxRetries       = 3;   // DM retry attempts (1-5)
    bool     requestTelemetry = true;
    String   showTelemetry    = "both";  // "battery", "location", "both", "none"
    bool     cannedMessages   = false;   // Enable canned message quick-reply picker
    std::vector<String> cannedCustom;    // Optional custom texts from config array
};

struct BatteryConfig {
    bool    lowAlertEnabled   = false;
    uint8_t lowAlertThreshold = 10;
};

struct SecurityConfig {
    bool   pinEnabled   = false;
    String pinCode      = "";
    bool   adminEnabled = true;
    bool   keyLockEnabled = true;
    bool   autoKeyLock    = false;
};

struct AppConfig {
    String          deviceName;
    String          language;    // "" = English, "de" = German, etc.
    RadioConfig     radio;
    String          privateKey;  // base64
    String          publicKey;   // base64
    std::vector<ContactConfig> contacts;
    std::vector<ChannelConfig> channels;
    DisplayConfig   display;
    MessagingConfig messaging;
    bool            soundEnabled = true;
    String          sosKeyword   = "SOS";
    uint8_t         sosRepeat    = 3;
    bool            gpsEnabled = true;
    int8_t          gpsClockOffset = 0;  // UTC offset in hours (legacy fallback)
    String          gpsTimezone;         // POSIX TZ string for auto-DST (e.g. "CET-1CEST,M3.5.0/2,M10.5.0/3")
    uint16_t        gpsLastKnownMaxAge = 1800;  // Seconds before last-known expires
    BatteryConfig   battery;
    SecurityConfig  security;
};

class ConfigManager {
public:
    enum LoadResult { LOAD_OK, LOAD_NO_FILE, LOAD_ERROR };
    LoadResult load();   // Load from SD card
    bool save();         // Save current config to SD card
    bool generate();     // Generate default config with new identity

    AppConfig& config() { return _config; }
    const AppConfig& config() const { return _config; }

    bool hasIdentity() const;
    bool hasContacts() const;

    static ConfigManager& instance();

private:
    ConfigManager() = default;
    AppConfig _config;

    void applyDefaults();
    bool parseJson(const String& json);
    String toJson() const;
};

}  // namespace mclite
