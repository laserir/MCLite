#pragma once

#include <ArduinoJson.h>
#include <Arduino.h>
#include <vector>
#include <utility>
#include <cstdint>

namespace mclite {

// A scope/region field holds a SINGLE region name ("eug"), "*", or "" — never a
// space-separated list. A pasted multi-region `region def` string (e.g.
// "west pnw or wv eug") would be hashed as one name, producing a transport key
// that matches no repeater, so every scoped flood is silently dropped (#36).
// Keep only the first whitespace-delimited token. Returns true if it changed the
// input, so callers can log the correction. Applied to every scope input path
// (config.json load, companion set-scope, on-device editor).
inline bool sanitizeScope(String& s) {
    String before = s;
    s.trim();
    int sp = s.indexOf(" ");
    if (sp >= 0) s = s.substring(0, sp);
    return s != before;
}

struct ContactConfig {
    String alias;            // Display name (user-chosen)
    String publicKey;        // base64
    bool   allowTelemetry = true;   // Base telemetry permission (must be true for location/environment to work)
    bool   allowLocation  = false;  // Respond to GPS location requests (requires allow_telemetry)
    bool   allowEnvironment = false;  // Respond to environment sensor requests (requires allow_telemetry)
    bool   alwaysSound    = false;  // Play notification even when muted
    bool   allowSos       = true;   // Allow SOS alerts from this contact
    bool   sendSos        = true;   // Include in outgoing SOS broadcast
    bool   fromDiscovery  = false;  // Auto-added from heard adverts; review me
    std::vector<String> canned;     // Per-conversation quick replies (overrides global; empty = use global)
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
    std::vector<String> canned;  // Per-conversation quick replies (overrides global; empty = use global)
};

struct RoomServerConfig {
    String name;       // Display name (user-chosen)
    String publicKey;  // 64 hex chars (server's Ed25519 public key)
    String password;   // Plaintext, ≤15 chars; "" = public room
    bool   allowSos = true;   // Trigger SOS alert when a room post starts with the SOS keyword
    bool   sendSos  = false;  // Include this room in outgoing SOS broadcasts (default off — community rooms shouldn't be spammed)
    bool   readOnly = false;  // Hide input bar in chat view (listen-only)
    String scope;             // Region scope override ("" = inherit global, "*" = wildcard, "#name" = region)
    std::vector<String> canned;  // Per-conversation quick replies (overrides global; empty = use global)
};

struct RadioConfig {
    float   frequency       = 869.618f;
    uint8_t spreadingFactor = 8;
    float   bandwidth       = 62.5f;
    int8_t  txPower         = 22;
    uint8_t codingRate      = 8;
    String  scope           = "*";  // Region scope ("*" = no transport codes, "#name" = region)
    uint8_t pathHashMode    = 0;    // 0/1/2 → 1/2/3 bytes per repeater hash in path
    uint16_t advertIntervalMin = 0; // Periodic flood-advert interval in minutes. 0 = off (default).
                                    // Off avoids spamming shared meshes (issue #13); if set, >=60.
};

// A user-defined palette from config (display.themes[]). Stored as strings only
// — no LVGL types — so ConfigManager stays host-testable; the theme layer
// resolves these into an actual palette at boot. Unspecified keys inherit `base`.
struct CustomTheme {
    String name;
    String base = "dark";                                  // built-in to start from
    std::vector<std::pair<String, String>> colors;         // canonical key -> "#RRGGBB"
};

struct DisplayConfig {
    uint8_t  brightness     = 180;
    uint16_t autoDimSeconds = 30;
    String   theme          = "dark";  // active theme: a built-in or a custom name
    String   bootText       = "";   // Optional text shown on boot screen below version
    uint8_t  dimBrightness  = 0;     // Brightness when dimmed (0 = screen off)
    bool     kbdBacklight   = true;  // Keyboard backlight follows auto-dim (on/off)
    uint8_t  kbdBrightness  = 127;   // Keyboard backlight level (1-255)
    bool     emoji          = true;  // Show the chat emoji picker (received emoji always render)
    bool     menuButton     = false; // T-Deck only: status-bar gear that opens Admin
    std::vector<CustomTheme> customThemes;  // optional user palettes (display.themes[])
};

struct MessagingConfig {
    bool     saveHistory      = true;
    uint16_t maxHistoryPerChat = 100;
    String   locationFormat   = "decimal";
    uint8_t  maxRetries       = 3;   // DM retry attempts (1-5)
    bool     requestTelemetry = true;
    String   showTelemetry    = "both";  // "battery", "location", "both", "none"
    bool     cannedMessages   = true;    // Enable canned message quick-reply picker
    std::vector<String> cannedCustom;    // Optional custom texts from config array
    bool     allowMute        = false;   // Allow per-chat mute via long-press (off by default)
    bool     autoTelemetry    = false;   // Periodically auto-refresh contacts' GPS via telemetry (opt-in, default off)
    bool     shareContact     = true;    // Show the chat-header Share button (re-broadcast a contact's advert)
    bool     showHopCount     = false;   // Show received-message hop count (xN) in the chat bubble (default off)
};

struct BatteryConfig {
    bool    lowAlertEnabled   = false;
    uint8_t lowAlertThreshold = 10;
};

struct SecurityConfig {
    String lockMode     = "key";   // "none", "key", "pin"
    String pinCode      = "";
    String autoLock     = "key";   // "none", "key", "pin"
    bool   adminEnabled = true;
};

// Step-wise on-device permissions, applied *within* the Admin gate
// (security.adminEnabled stays the global "can Admin open at all" switch).
struct PermissionsConfig {
    String settings = "full";              // "full" | "restricted" | "none"
    bool   conversationManagement = true;  // on-device add/remove of contacts/channels/rooms
    bool   companion = true;               // show the Companion group in Admin
};

struct OffgridConfig {
    bool enabled = false;  // When true, forward packets + switch to closest offgrid freq (433/869/918)
};

struct WiFiConfig {
    String ssid;                 // "" = WiFi disabled / not configured
    String password;
    bool   autoUpdate = false;   // Check GitHub for a newer firmware on boot (over WiFi) and prompt
};

struct BleConfig {
    uint32_t pin = 0;            // BLE companion pairing passkey. 0 = auto-generate
                                 // a random 6-digit PIN on first use and persist it.
};

struct DebugConfig {
    bool showMemory = false;     // Enable display of memory usage in status bar.
    bool screenshots = false;    // Enable save-screen-to-SD (/screenshots/*.bmp). Debug aid, default off.
};

struct AppConfig {
    String          deviceName;
    String          language;    // "" = English, "de" = German, etc.
    RadioConfig     radio;
    String          privateKey;  // base64
    String          publicKey;   // base64
    std::vector<ContactConfig> contacts;
    std::vector<ChannelConfig> channels;
    std::vector<RoomServerConfig> roomServers;
    DisplayConfig   display;
    MessagingConfig messaging;
    bool            soundEnabled = true;
    String          sosKeyword   = "SOS";
    uint8_t         sosRepeat    = 3;
    bool            gpsEnabled = true;
    int8_t          gpsClockOffset = 0;  // UTC offset in hours (legacy fallback)
    String          gpsTimezone;         // POSIX TZ string for auto-DST (e.g. "CET-1CEST,M3.5.0/2,M10.5.0/3")
    uint16_t        gpsLastKnownMaxAge = 1800;  // Seconds before last-known expires
    uint8_t         locationPrecision = 0;  // Location-advert precision: 0=off, 32=exact, 10-31=coarsened grid
    BatteryConfig   battery;
    SecurityConfig  security;
    PermissionsConfig permissions;
    OffgridConfig   offgrid;
    WiFiConfig      wifi;
    BleConfig       ble;
    DebugConfig     debug;
};

class ConfigManager {
public:
    enum LoadResult { LOAD_OK, LOAD_NO_FILE, LOAD_ERROR };
    LoadResult load();   // Load from SD card
    bool save();         // Save current config to SD card
    bool generate();     // Generate default config with new identity

    // Append a new contact (typically discovered via heard adverts) and save
    // immediately via writeAtomic. Returns false if pubkey already in
    // _config.contacts, or if the chat-contact cap is reached, or if the SD
    // write fails. Caller decides what to surface to the user (queued vs
    // duplicate vs full vs IO error). The new contact only becomes routable
    // after reboot, when ContactStore::loadFromConfig() picks it up and
    // MCLiteMesh::start() registers it with MeshCore.
    bool appendDiscoveredContact(const ContactConfig& cc);

    // On-device add/remove for conversations (gated by permissions.conversation_management
    // in the UI). All write config immediately via save() and roll back on SD failure;
    // changes only take effect after reboot (stores register with MeshCore at boot).
    // appendChannel: caps at MAX_CHANNELS, name unique (case-insensitive); a "public"-type
    //   channel is forced to the canonical name/PSK and refused if one already exists; a
    //   "private" channel requires a 32-hex PSK; hashtag PSK is derived at boot (psk left
    //   empty). Assigns index = vector position.
    // appendRoom: caps at MAX_ROOM_SERVERS, refuses duplicate pubkey (case-insensitive hex).
    // removeChannelAt: erases then renumbers remaining channels' index to their position.
    bool appendChannel(const ChannelConfig& cc);
    bool appendRoom(const RoomServerConfig& rc);
    bool removeContactAt(size_t i);
    bool removeChannelAt(size_t i);
    bool removeRoomAt(size_t i);
    bool hasPublicChannel() const;

    // Resolve a 32-byte raw public key to its index in _config.contacts, decoding each
    // stored key (hex-64 OR base64) so it matches regardless of storage format. Returns
    // -1 if not found. Unlike hasContactByPubkeyHex (a single-format string compare), this
    // is format-agnostic — required by the companion add/edit/remove path, where the app
    // sends raw key bytes and contacts may be stored either way.
    int findContactIndexByKey(const uint8_t* key32) const;

    // Update an existing contact's display alias (companion contact edit). Saves
    // immediately and rolls back on SD failure. Returns false on bad index or save fail.
    bool updateContactAlias(size_t i, const String& alias);

    AppConfig& config() { return _config; }
    const AppConfig& config() const { return _config; }

    bool hasIdentity() const;
    bool hasContacts() const;

    // Case-insensitive hex match against ContactConfig::publicKey. Used by
    // the heard-adverts UI to detect "already queued in this session" when
    // the cache entry's savePending flag has been lost to LRU eviction.
    // Doesn't match contacts whose publicKey is stored in base64 — those
    // are caught at boot-time by ContactStore::findByPublicKey instead.
    bool hasContactByPubkeyHex(const String& hexKey) const;

    static ConfigManager& instance();

private:
    ConfigManager() = default;
    AppConfig _config;

    void applyDefaults();
    bool parseJson(const String& json);
    String toJson() const;
};

}  // namespace mclite
