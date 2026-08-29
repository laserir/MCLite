#pragma once

// MCLite default configuration values

// All three are #ifndef-guarded so a fork can override them with -D build flags
// (e.g. -DMCLITE_REPO_OWNER=\"someone\") to self-update from its own GitHub repo.
#ifndef MCLITE_VERSION
#define MCLITE_VERSION "0.4.3"
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
constexpr const char* FIRMWARE_VENDOR  = MCLITE_REPO_OWNER "/" MCLITE_REPO_NAME;

// Translation-file schema version. Encodes the release it ships with, dotless
// (0.3.9 -> 39, 0.4.0 -> 40). Bump this whenever DEFAULT_STRINGS gains keys, and
// set the SAME number in each sdcard/mclite/lang/*.json "version". On boot I18n
// warns (serial) if a loaded lang file's version is older — i.e. it predates the
// current string set, so some keys will fall back to English.
constexpr uint16_t LANG_VERSION = 56;   // 0.4.x: + rxn_no_timestamp

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
constexpr uint16_t RADIO_ADVERT_INTERVAL_MIN = 0; // Periodic flood-advert interval (min). 0 = off (default)

// Contacts — must stay <= MAX_CONTACTS - MAX_ROOMS from platformio.ini (40 - 8 = 32).
// Save-to-contacts refuses to append when this is reached.
constexpr int      MAX_CHAT_CONTACTS     = 32;
// Channels (MeshCore MAX_GROUP_CHANNELS=16) and room servers (MAX_ROOMS_RUNTIME=8,
// part of the 40-contact MeshCore budget). On-device add refuses at these caps.
constexpr int      MAX_CHANNELS          = 16;
constexpr int      MAX_ROOM_SERVERS      = 8;

// Display
constexpr uint8_t  DISPLAY_BRIGHTNESS    = 180;
constexpr uint16_t AUTO_DIM_SECONDS      = 30;
constexpr const char* BOOT_TEXT          = "";
constexpr uint8_t  DIM_BRIGHTNESS        = 0;
constexpr bool     KBD_BACKLIGHT         = true;
constexpr uint8_t  KBD_BRIGHTNESS        = 127;
constexpr bool     EMOJI_ENABLED         = true;   // Chat emoji picker on by default
constexpr bool     COLOR_EMOJI           = true;   // Colour glyphs for the on-device emoji set
constexpr bool     MENU_BUTTON           = false;  // T-Deck only: status-bar gear -> Admin. Off by default

// Messaging
constexpr bool     SAVE_HISTORY          = true;
constexpr uint16_t MAX_HISTORY_PER_CHAT  = 100;
constexpr const char* LOCATION_FORMAT    = "decimal";
constexpr uint8_t  MAX_RETRIES           = 3;   // DM retry attempts (1-5)
constexpr bool     REQUEST_TELEMETRY     = true;
constexpr const char* SHOW_TELEMETRY    = "both";  // "battery", "location", "both", "none"
constexpr bool     CANNED_MESSAGES_ENABLED = true;
constexpr bool     ALLOW_MUTE            = false;  // Enable per-chat mute (long-press); off by default
constexpr bool     AUTO_TELEMETRY        = false;  // Auto-refresh contacts' GPS via periodic telemetry (opt-in)
constexpr bool     SHARE_CONTACT         = true;   // Chat-header Share button (re-broadcast a contact's advert)
constexpr bool     SHOW_HOP_COUNT        = false;  // Show received-message hop count (xN) in the chat bubble; off by default
constexpr bool     REACTIONS_ENABLED     = false;  // Emoji reactions (MeshCore One interop); off by default

// Auto-telemetry scheduler tuning (background GPS refresh for contacts who don't advert location)
constexpr uint32_t AUTO_TELEM_SCAN_MS        = 60000;    // Evaluate at most one request per minute
constexpr uint32_t AUTO_TELEM_REFRESH_AGE_MS = 1500000;  // 25 min — refresh just before the 30-min stale window
constexpr uint8_t  AUTO_TELEM_MAX_MISSES     = 2;        // Stop asking a non-responder after N misses (session)
// Backstop for one auto request. Normally a miss is concluded as soon as the
// mesh finishes its own request+flood-retry exchange (telemetryPending() goes
// false); this deadline only frees the slot if the mesh somehow never resolves
// (e.g. a UI request hijacked it). Generous so it never pre-empts a legitimate
// multi-hop flood-retry window and false-counts a miss.
constexpr uint32_t AUTO_TELEM_AWAIT_MS       = 120000;
// Max message text size in BYTES (UTF-8), matching MeshCore MAX_TEXT_LEN (10*16).
// The chat textarea caps at 160 *characters*, but multi-byte chars (emoji, accents)
// can exceed this byte budget — validate byte length before sending.
constexpr uint16_t MAX_MSG_BYTES         = 160;

// Sound
constexpr bool     SOUND_ENABLED         = true;
constexpr const char* SOS_KEYWORD        = "SOS";
constexpr uint8_t  SOS_REPEAT            = 3;

// GPS
constexpr bool     GPS_ENABLED           = true;
constexpr int8_t   GPS_CLOCK_OFFSET      = 0;    // UTC offset in hours (-12 to +14) — legacy fallback
constexpr const char* GPS_TIMEZONE        = "";   // POSIX TZ string (empty = use clock_offset)
constexpr uint16_t GPS_LAST_KNOWN_MAX_AGE = 1800; // Seconds (30 min) before last-known becomes NO_FIX
constexpr uint8_t  GPS_LOCATION_PRECISION = 0;    // Location-advert precision (0=off, 32=exact, 10-31=coarse)

// Battery
constexpr bool     BATTERY_LOW_ALERT_ENABLED   = false;
constexpr uint8_t  BATTERY_LOW_ALERT_THRESHOLD = 10;

// Security
constexpr const char* LOCK_MODE          = "key";   // "none", "key", "pin"
constexpr const char* PIN_CODE           = "";
constexpr const char* AUTO_LOCK          = "key";   // "none", "key", "pin"
constexpr bool     ADMIN_ENABLED         = true;
constexpr const char* ADMIN_PIN          = "";     // Re-enables Admin after a lockout; 4-8 chars, empty = unset
constexpr bool     SCREENSHOTS_ENABLED   = false;  // debug.screenshots — save-screen-to-SD, off by default

// Permissions (within the Admin gate). admin_enabled stays the global on/off.
constexpr const char* PERM_SETTINGS      = "full";  // "full" | "restricted" | "none"
constexpr bool     PERM_CONVERSATION_MGMT = true;   // on-device add/remove of contacts/channels/rooms
constexpr bool     PERM_COMPANION        = true;    // show the Companion group in Admin

// Language
constexpr const char* LANGUAGE = "";  // "" = English (default)

// Config file path on SD card
constexpr const char* CONFIG_PATH        = "/config.json";
constexpr const char* HISTORY_DIR        = "/mclite/history";

}  // namespace defaults
}  // namespace mclite
