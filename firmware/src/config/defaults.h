#pragma once

// MCLite default configuration values

// All three are #ifndef-guarded so a fork can override them with -D build flags
// (e.g. -DMCLITE_REPO_OWNER=\"someone\") to self-update from its own GitHub repo.
#ifndef MCLITE_VERSION
#define MCLITE_VERSION "0.3.2"
#endif
#ifndef MCLITE_REPO_OWNER
#define MCLITE_REPO_OWNER "laserir"   // GitHub owner for OTA update checks
#endif
#ifndef MCLITE_REPO_NAME
#define MCLITE_REPO_NAME "MCLite"     // GitHub repo for OTA update checks
#endif

namespace mclite {
namespace defaults {

// Firmware
constexpr const char* FIRMWARE_VERSION = MCLITE_VERSION;

// Device
constexpr const char* DEVICE_NAME = "MCLite";

// Radio (LoRa) — defaults match EU/UK/CH preset
constexpr float    RADIO_FREQUENCY       = 869.618f;
constexpr uint8_t  RADIO_SPREADING_FACTOR = 8;
constexpr float    RADIO_BANDWIDTH       = 62.5f;
constexpr int8_t   RADIO_TX_POWER        = 22;
constexpr uint8_t  RADIO_CODING_RATE     = 8;
constexpr const char* RADIO_SCOPE        = "*";  // No transport codes (wildcard)
constexpr uint8_t  RADIO_PATH_HASH_MODE  = 0;    // 0=1B/hop (legacy), 1=2B/hop, 2=3B/hop

// Contacts — must stay <= MAX_CONTACTS - MAX_ROOMS from platformio.ini (40 - 8 = 32).
// Save-to-contacts refuses to append when this is reached.
constexpr int      MAX_CHAT_CONTACTS     = 32;

// Display
constexpr uint8_t  DISPLAY_BRIGHTNESS    = 180;
constexpr uint16_t AUTO_DIM_SECONDS      = 30;
constexpr const char* BOOT_TEXT          = "";
constexpr uint8_t  DIM_BRIGHTNESS        = 0;
constexpr bool     KBD_BACKLIGHT         = true;
constexpr uint8_t  KBD_BRIGHTNESS        = 127;

// Messaging
constexpr bool     SAVE_HISTORY          = true;
constexpr uint16_t MAX_HISTORY_PER_CHAT  = 100;
constexpr const char* LOCATION_FORMAT    = "decimal";
constexpr uint8_t  MAX_RETRIES           = 3;   // DM retry attempts (1-5)
constexpr bool     REQUEST_TELEMETRY     = true;
constexpr const char* SHOW_TELEMETRY    = "both";  // "battery", "location", "both", "none"
constexpr bool     CANNED_MESSAGES_ENABLED = true;
constexpr bool     ALLOW_MUTE            = false;  // Enable per-chat mute (long-press); off by default

// Sound
constexpr bool     SOUND_ENABLED         = true;
constexpr const char* SOS_KEYWORD        = "SOS";
constexpr uint8_t  SOS_REPEAT            = 3;

// GPS
constexpr bool     GPS_ENABLED           = true;
constexpr int8_t   GPS_CLOCK_OFFSET      = 0;    // UTC offset in hours (-12 to +14) — legacy fallback
constexpr const char* GPS_TIMEZONE        = "";   // POSIX TZ string (empty = use clock_offset)
constexpr uint16_t GPS_LAST_KNOWN_MAX_AGE = 1800; // Seconds (30 min) before last-known becomes NO_FIX
constexpr bool     GPS_LOCATION_ADVERT   = false; // Include own (LIVE/last-known) location in adverts

// Battery
constexpr bool     BATTERY_LOW_ALERT_ENABLED   = false;
constexpr uint8_t  BATTERY_LOW_ALERT_THRESHOLD = 10;

// Security
constexpr const char* LOCK_MODE          = "key";   // "none", "key", "pin"
constexpr const char* PIN_CODE           = "";
constexpr const char* AUTO_LOCK          = "key";   // "none", "key", "pin"
constexpr bool     ADMIN_ENABLED         = true;

// Language
constexpr const char* LANGUAGE = "";  // "" = English (default)

// Config file path on SD card
constexpr const char* CONFIG_PATH        = "/config.json";
constexpr const char* HISTORY_DIR        = "/mclite/history";

}  // namespace defaults
}  // namespace mclite
