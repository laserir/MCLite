#include <Arduino.h>
#include <map>
#include <string>

// Make parseJson + SDCard::_mounted accessible
#define private public
#include "config/ConfigManager.h"
#include "storage/SDCard.h"
#undef private

#include "config/defaults.h"

// Pull in ConfigManager implementation
#include "config/ConfigManager.cpp"

// In-memory filesystem stub. Tests set entries via setStubFile().
namespace {
    std::map<std::string, std::string> g_stub_files;
    std::string g_last_atomic_path;
    std::string g_last_atomic_content;

    void setStubFile(const char* path, const String& content) {
        g_stub_files[std::string(path)] = std::string(content.c_str());
    }
    void clearStubFiles() {
        g_stub_files.clear();
        g_last_atomic_path.clear();
        g_last_atomic_content.clear();
    }
    String getStubFile(const char* path) {
        auto it = g_stub_files.find(std::string(path));
        return it == g_stub_files.end() ? String() : String(it->second.c_str());
    }
}

// Stub SDCard methods backed by g_stub_files.
namespace mclite {
    SDCard& SDCard::instance() { static SDCard inst; return inst; }

    bool SDCard::fileExists(const char* path) {
        return g_stub_files.count(std::string(path)) > 0;
    }
    String SDCard::readFile(const char* path, size_t) {
        auto it = g_stub_files.find(std::string(path));
        return it == g_stub_files.end() ? String() : String(it->second.c_str());
    }
    bool SDCard::writeFile(const char* path, const String& content) {
        g_stub_files[std::string(path)] = std::string(content.c_str());
        return true;
    }
    bool SDCard::remove(const char* path) {
        return g_stub_files.erase(std::string(path)) > 0;
    }

    // Tests don't exercise the actual rename dance; they just verify that
    // ConfigManager::save() funnels content through writeAtomic and lands at
    // the requested path.
    bool SDCard::writeAtomic(const char* path, const String& content) {
        g_last_atomic_path    = path;
        g_last_atomic_content = content.c_str();
        g_stub_files[std::string(path)] = std::string(content.c_str());
        return true;
    }
}

#include <unity.h>

using namespace mclite;

static ConfigManager* cfg;

void setUp() {
    cfg = &ConfigManager::instance();
    // Reset to defaults before each test
    cfg->config() = AppConfig{};
    clearStubFiles();
    SDCard::instance()._mounted = true;
}

void tearDown() {}

// --- Helper: parse JSON and return success ---
static bool parse(const char* json) {
    return cfg->parseJson(String(json));
}

// ═══ Invalid JSON ═══

void test_invalid_json_rejected() {
    TEST_ASSERT_FALSE(parse("{broken json!!!"));
}

void test_empty_json_accepted() {
    // Empty object is valid — everything gets defaults
    TEST_ASSERT_TRUE(parse("{}"));
}

// ═══ Permissions ═══

void test_permissions_default_full() {
    TEST_ASSERT_TRUE(parse("{}"));
    TEST_ASSERT_EQUAL_STRING("full", cfg->config().permissions.settings.c_str());
    TEST_ASSERT_TRUE(cfg->config().permissions.companion);
    TEST_ASSERT_TRUE(cfg->config().permissions.conversationManagement);
}

void test_permissions_round_trip() {
    TEST_ASSERT_TRUE(parse("{\"permissions\":{\"settings\":\"restricted\","
                           "\"companion\":false,\"conversation_management\":true}}"));
    TEST_ASSERT_EQUAL_STRING("restricted", cfg->config().permissions.settings.c_str());
    TEST_ASSERT_FALSE(cfg->config().permissions.companion);
    TEST_ASSERT_TRUE(cfg->config().permissions.conversationManagement);
}

void test_permissions_none_mode() {
    TEST_ASSERT_TRUE(parse("{\"permissions\":{\"settings\":\"none\"}}"));
    TEST_ASSERT_EQUAL_STRING("none", cfg->config().permissions.settings.c_str());
}

void test_permissions_invalid_settings_falls_back() {
    TEST_ASSERT_TRUE(parse("{\"permissions\":{\"settings\":\"bogus\"}}"));
    TEST_ASSERT_EQUAL_STRING("full", cfg->config().permissions.settings.c_str());
}

// ═══ Radio bounds checking ═══

void test_radio_frequency_too_low() {
    parse("{\"radio\":{\"frequency\": 50.0}}");
    TEST_ASSERT_EQUAL_FLOAT(defaults::RADIO_FREQUENCY, cfg->config().radio.frequency);
}

void test_radio_frequency_too_high() {
    parse("{\"radio\":{\"frequency\": 1200.0}}");
    TEST_ASSERT_EQUAL_FLOAT(defaults::RADIO_FREQUENCY, cfg->config().radio.frequency);
}

void test_radio_frequency_valid() {
    parse("{\"radio\":{\"frequency\": 915.0}}");
    TEST_ASSERT_EQUAL_FLOAT(915.0f, cfg->config().radio.frequency);
}

void test_radio_sf_too_low() {
    parse("{\"radio\":{\"spreading_factor\": 2}}");
    TEST_ASSERT_EQUAL_UINT8(defaults::RADIO_SPREADING_FACTOR, cfg->config().radio.spreadingFactor);
}

void test_radio_sf_too_high() {
    parse("{\"radio\":{\"spreading_factor\": 15}}");
    TEST_ASSERT_EQUAL_UINT8(defaults::RADIO_SPREADING_FACTOR, cfg->config().radio.spreadingFactor);
}

void test_radio_sf_valid() {
    parse("{\"radio\":{\"spreading_factor\": 7}}");
    TEST_ASSERT_EQUAL_UINT8(7, cfg->config().radio.spreadingFactor);
}

void test_radio_tx_power_too_high() {
    parse("{\"radio\":{\"tx_power\": 50}}");
    TEST_ASSERT_EQUAL_INT8(defaults::RADIO_TX_POWER, cfg->config().radio.txPower);
}

void test_radio_tx_power_too_low() {
    parse("{\"radio\":{\"tx_power\": -20}}");
    TEST_ASSERT_EQUAL_INT8(defaults::RADIO_TX_POWER, cfg->config().radio.txPower);
}

void test_radio_coding_rate_bounds() {
    parse("{\"radio\":{\"coding_rate\": 3}}");
    TEST_ASSERT_EQUAL_UINT8(defaults::RADIO_CODING_RATE, cfg->config().radio.codingRate);
    parse("{\"radio\":{\"coding_rate\": 10}}");
    TEST_ASSERT_EQUAL_UINT8(defaults::RADIO_CODING_RATE, cfg->config().radio.codingRate);
}

void test_radio_bandwidth_bounds() {
    parse("{\"radio\":{\"bandwidth\": 1.0}}");
    TEST_ASSERT_EQUAL_FLOAT(defaults::RADIO_BANDWIDTH, cfg->config().radio.bandwidth);
    parse("{\"radio\":{\"bandwidth\": 600.0}}");
    TEST_ASSERT_EQUAL_FLOAT(defaults::RADIO_BANDWIDTH, cfg->config().radio.bandwidth);
}

// ═══ Device name truncation ═══

void test_device_name_truncated_at_20() {
    parse("{\"device\":{\"name\": \"ABCDEFGHIJKLMNOPQRSTUVWXYZ\"}}");
    TEST_ASSERT_EQUAL(20, cfg->config().deviceName.length());
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMNOPQRST", cfg->config().deviceName.c_str());
}

void test_device_name_normal() {
    parse("{\"device\":{\"name\": \"MyDevice\"}}");
    TEST_ASSERT_EQUAL_STRING("MyDevice", cfg->config().deviceName.c_str());
}

// ═══ Constrained fields ═══

void test_max_retries_clamped_low() {
    parse("{\"messaging\":{\"max_retries\": 0}}");
    TEST_ASSERT_EQUAL_UINT8(1, cfg->config().messaging.maxRetries);
}

void test_max_retries_clamped_high() {
    parse("{\"messaging\":{\"max_retries\": 100}}");
    TEST_ASSERT_EQUAL_UINT8(5, cfg->config().messaging.maxRetries);
}

void test_max_retries_valid() {
    parse("{\"messaging\":{\"max_retries\": 2}}");
    TEST_ASSERT_EQUAL_UINT8(2, cfg->config().messaging.maxRetries);
}

void test_sos_repeat_clamped() {
    parse("{\"sound\":{\"sos_repeat\": 0}}");
    TEST_ASSERT_EQUAL_UINT8(1, cfg->config().sosRepeat);
    parse("{\"sound\":{\"sos_repeat\": 99}}");
    TEST_ASSERT_EQUAL_UINT8(10, cfg->config().sosRepeat);
}

void test_gps_clock_offset_clamped() {
    parse("{\"gps\":{\"clock_offset\": -20}}");
    TEST_ASSERT_EQUAL_INT8(-12, cfg->config().gpsClockOffset);
    parse("{\"gps\":{\"clock_offset\": 20}}");
    TEST_ASSERT_EQUAL_INT8(14, cfg->config().gpsClockOffset);
}

void test_gps_last_known_max_age_clamped() {
    parse("{\"gps\":{\"last_known_max_age\": 10}}");
    TEST_ASSERT_EQUAL_UINT16(60, cfg->config().gpsLastKnownMaxAge);
    parse("{\"gps\":{\"last_known_max_age\": 9000}}");
    TEST_ASSERT_EQUAL_UINT16(7200, cfg->config().gpsLastKnownMaxAge);
}

void test_battery_threshold_clamped() {
    parse("{\"battery\":{\"low_alert_threshold\": 1}}");
    TEST_ASSERT_EQUAL_UINT8(5, cfg->config().battery.lowAlertThreshold);
    parse("{\"battery\":{\"low_alert_threshold\": 90}}");
    TEST_ASSERT_EQUAL_UINT8(50, cfg->config().battery.lowAlertThreshold);
}

void test_kbd_brightness_clamped() {
    parse("{\"display\":{\"kbd_brightness\": 0}}");
    TEST_ASSERT_EQUAL_UINT8(1, cfg->config().display.kbdBrightness);
}

// ═══ Enum validation ═══

void test_show_telemetry_valid_values() {
    parse("{\"messaging\":{\"show_telemetry\": \"battery\"}}");
    TEST_ASSERT_EQUAL_STRING("battery", cfg->config().messaging.showTelemetry.c_str());

    parse("{\"messaging\":{\"show_telemetry\": \"location\"}}");
    TEST_ASSERT_EQUAL_STRING("location", cfg->config().messaging.showTelemetry.c_str());

    parse("{\"messaging\":{\"show_telemetry\": \"none\"}}");
    TEST_ASSERT_EQUAL_STRING("none", cfg->config().messaging.showTelemetry.c_str());
}

void test_show_telemetry_invalid_falls_back() {
    parse("{\"messaging\":{\"show_telemetry\": \"garbage\"}}");
    TEST_ASSERT_EQUAL_STRING(defaults::SHOW_TELEMETRY, cfg->config().messaging.showTelemetry.c_str());
}

// ═══ Contacts ═══

void test_contact_empty_key_skipped() {
    parse("{\"contacts\":[{\"alias\":\"Alice\",\"public_key\":\"\"}]}");
    TEST_ASSERT_EQUAL(0, cfg->config().contacts.size());
}

void test_contact_with_key_accepted() {
    parse("{\"contacts\":[{\"alias\":\"Alice\",\"public_key\":\"AAAA\"}]}");
    TEST_ASSERT_EQUAL(1, cfg->config().contacts.size());
    TEST_ASSERT_EQUAL_STRING("Alice", cfg->config().contacts[0].alias.c_str());
}

void test_contact_defaults() {
    parse("{\"contacts\":[{\"alias\":\"Bob\",\"public_key\":\"BBBB\"}]}");
    const auto& c = cfg->config().contacts[0];
    TEST_ASSERT_TRUE(c.allowTelemetry);
    TEST_ASSERT_FALSE(c.allowLocation);
    TEST_ASSERT_FALSE(c.allowEnvironment);
    TEST_ASSERT_FALSE(c.alwaysSound);
    TEST_ASSERT_TRUE(c.allowSos);
    TEST_ASSERT_TRUE(c.sendSos);
}

// ═══ Channels ═══

void test_channel_duplicate_index_skipped() {
    parse("{\"channels\":["
          "{\"name\":\"#one\",\"type\":\"hashtag\",\"index\":0},"
          "{\"name\":\"#two\",\"type\":\"hashtag\",\"index\":0}"
          "]}");
    TEST_ASSERT_EQUAL(1, cfg->config().channels.size());
    TEST_ASSERT_EQUAL_STRING("#one", cfg->config().channels[0].name.c_str());
}

void test_private_channel_without_psk_skipped() {
    parse("{\"channels\":[{\"name\":\"Secret\",\"type\":\"private\",\"psk\":\"\",\"index\":0}]}");
    TEST_ASSERT_EQUAL(0, cfg->config().channels.size());
}

void test_channel_send_sos_type_aware_default() {
    // Hashtag/public channels default send_sos=false (community-spam avoidance)
    // Private channels default send_sos=true (trusted small group)
    parse("{\"channels\":["
          "{\"name\":\"#tag\",\"type\":\"hashtag\",\"index\":0},"
          "{\"name\":\"P\",\"type\":\"private\",\"psk\":\"a1b2c3d4e5f6a7b8a1b2c3d4e5f6a7b8\",\"index\":1}"
          "]}");
    TEST_ASSERT_EQUAL(2, cfg->config().channels.size());
    TEST_ASSERT_FALSE(cfg->config().channels[0].sendSos);  // hashtag → false
    TEST_ASSERT_TRUE(cfg->config().channels[1].sendSos);   // private → true
}

void test_channel_send_sos_explicit_overrides_default() {
    parse("{\"channels\":["
          "{\"name\":\"#shout\",\"type\":\"hashtag\",\"index\":0,\"send_sos\":true},"
          "{\"name\":\"Q\",\"type\":\"private\",\"psk\":\"a1b2c3d4e5f6a7b8a1b2c3d4e5f6a7b8\",\"index\":1,\"send_sos\":false}"
          "]}");
    TEST_ASSERT_TRUE(cfg->config().channels[0].sendSos);   // explicit true wins
    TEST_ASSERT_FALSE(cfg->config().channels[1].sendSos);  // explicit false wins
}

void test_hashtag_channel_without_psk_accepted() {
    // Hashtag channels derive PSK from name — no explicit PSK needed
    parse("{\"channels\":[{\"name\":\"#test\",\"type\":\"hashtag\",\"index\":0}]}");
    TEST_ASSERT_EQUAL(1, cfg->config().channels.size());
}

// ═══ Canned messages ═══

void test_canned_messages_bool_true() {
    parse("{\"messaging\":{\"canned_messages\": true}}");
    TEST_ASSERT_TRUE(cfg->config().messaging.cannedMessages);
}

void test_canned_messages_bool_false() {
    parse("{\"messaging\":{\"canned_messages\": false}}");
    TEST_ASSERT_FALSE(cfg->config().messaging.cannedMessages);
}

void test_canned_messages_array_enables_and_stores() {
    parse("{\"messaging\":{\"canned_messages\": [\"Help\", \"OK\", \"Wait\"]}}");
    TEST_ASSERT_TRUE(cfg->config().messaging.cannedMessages);
    TEST_ASSERT_EQUAL(3, cfg->config().messaging.cannedCustom.size());
    TEST_ASSERT_EQUAL_STRING("Help", cfg->config().messaging.cannedCustom[0].c_str());
}

void test_canned_messages_array_max_8() {
    parse("{\"messaging\":{\"canned_messages\": [\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\",\"8\",\"9\",\"10\"]}}");
    TEST_ASSERT_EQUAL(8, cfg->config().messaging.cannedCustom.size());
}

// ═══ Per-conversation quick replies (canned override) ═══

void test_contact_canned_parsed() {
    parse("{\"contacts\":[{\"alias\":\"Bot\",\"public_key\":\"AAAA\",\"canned\":[\"Open gate\",\"Lights on\"]}]}");
    const auto& c = cfg->config().contacts[0];
    TEST_ASSERT_EQUAL(2, c.canned.size());
    TEST_ASSERT_EQUAL_STRING("Open gate", c.canned[0].c_str());
    TEST_ASSERT_EQUAL_STRING("Lights on", c.canned[1].c_str());
}

void test_channel_canned_parsed() {
    parse("{\"channels\":[{\"name\":\"#ops\",\"type\":\"hashtag\",\"index\":0,\"canned\":[\"Status?\"]}]}");
    TEST_ASSERT_EQUAL(1, cfg->config().channels[0].canned.size());
    TEST_ASSERT_EQUAL_STRING("Status?", cfg->config().channels[0].canned[0].c_str());
}

void test_room_canned_parsed() {
    parse("{\"room_servers\":[{\"name\":\"HA\",\"public_key\":\"abcd\",\"canned\":[\"A\",\"B\",\"C\"]}]}");
    TEST_ASSERT_EQUAL(3, cfg->config().roomServers[0].canned.size());
    TEST_ASSERT_EQUAL_STRING("C", cfg->config().roomServers[0].canned[2].c_str());
}

void test_contact_canned_absent_is_empty() {
    parse("{\"contacts\":[{\"alias\":\"Bob\",\"public_key\":\"BBBB\"}]}");
    TEST_ASSERT_EQUAL(0, cfg->config().contacts[0].canned.size());
}

void test_canned_blanks_skipped_and_capped() {
    // 10 entries, one blank → blank dropped, remainder capped at 8
    parse("{\"contacts\":[{\"alias\":\"B\",\"public_key\":\"AAAA\","
          "\"canned\":[\"a\",\"\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\",\"h\",\"i\"]}]}");
    const auto& c = cfg->config().contacts[0];
    TEST_ASSERT_EQUAL(8, c.canned.size());
    TEST_ASSERT_EQUAL_STRING("a", c.canned[0].c_str());
    TEST_ASSERT_EQUAL_STRING("b", c.canned[1].c_str());  // blank at index 1 skipped
}

void test_canned_global_array_roundtrips() {
    // Bug 1 regression: a global custom array must survive save() (it was written as a bare
    // bool, dropping the list on the next boot).
    parse("{\"messaging\":{\"canned_messages\":[\"Help\",\"OK\",\"Wait\"]}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_TRUE(cfg->config().messaging.cannedMessages);
    TEST_ASSERT_EQUAL(3, cfg->config().messaging.cannedCustom.size());
    TEST_ASSERT_EQUAL_STRING("Wait", cfg->config().messaging.cannedCustom[2].c_str());
}

void test_canned_global_off_roundtrips() {
    // Honor the on/off bool: a disabled toggle stays disabled across save().
    parse("{\"messaging\":{\"canned_messages\":false}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_FALSE(cfg->config().messaging.cannedMessages);
    TEST_ASSERT_EQUAL(0, cfg->config().messaging.cannedCustom.size());
}

void test_canned_array_then_bool_false_clears() {
    // Regression guard: parseJson must clear cannedCustom before the canned_messages branch,
    // so a second parse with the toggle off does not retain the first parse's custom list.
    parse("{\"messaging\":{\"canned_messages\":[\"A\",\"B\"]}}");
    TEST_ASSERT_EQUAL(2, cfg->config().messaging.cannedCustom.size());
    cfg->parseJson(String("{\"messaging\":{\"canned_messages\":false}}"));  // no AppConfig reset
    TEST_ASSERT_FALSE(cfg->config().messaging.cannedMessages);
    TEST_ASSERT_EQUAL(0, cfg->config().messaging.cannedCustom.size());
}

void test_canned_array_then_absent_key_clears() {
    // canned_messages key absent but the messaging section present (e.g. an older/edited config):
    // the custom list from a prior parse must not survive.
    parse("{\"messaging\":{\"canned_messages\":[\"A\",\"B\"]}}");
    cfg->parseJson(String("{\"messaging\":{}}"));
    TEST_ASSERT_EQUAL(0, cfg->config().messaging.cannedCustom.size());
}

void test_canned_roundtrips_through_serialize() {
    parse("{\"contacts\":[{\"alias\":\"Bot\",\"public_key\":\"AAAA\",\"canned\":[\"X\",\"Y\"]}],"
          "\"channels\":[{\"name\":\"#c\",\"type\":\"hashtag\",\"index\":0,\"canned\":[\"Z\"]}],"
          "\"room_servers\":[{\"name\":\"R\",\"public_key\":\"abcd\",\"canned\":[\"Q\"]}]}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_EQUAL(2, cfg->config().contacts[0].canned.size());
    TEST_ASSERT_EQUAL_STRING("Y", cfg->config().contacts[0].canned[1].c_str());
    TEST_ASSERT_EQUAL(1, cfg->config().channels[0].canned.size());
    TEST_ASSERT_EQUAL_STRING("Z", cfg->config().channels[0].canned[0].c_str());
    TEST_ASSERT_EQUAL(1, cfg->config().roomServers[0].canned.size());
    TEST_ASSERT_EQUAL_STRING("Q", cfg->config().roomServers[0].canned[0].c_str());
}

void test_empty_canned_omitted_from_serialize() {
    parse("{\"contacts\":[{\"alias\":\"Bob\",\"public_key\":\"AAAA\"}]}");
    String json = cfg->toJson();
    // The global "canned_messages" key is always present; the per-conversation
    // "canned" array must NOT be (empty → omitted).
    TEST_ASSERT_NULL(strstr(json.c_str(), "\"canned\""));
}

// ═══ Auto-telemetry (auto-refresh contact GPS) ═══

void test_auto_telemetry_defaults_off() {
    parse("{}");
    TEST_ASSERT_FALSE(cfg->config().messaging.autoTelemetry);
}

void test_auto_telemetry_explicit_true() {
    parse("{\"messaging\":{\"auto_telemetry\": true}}");
    TEST_ASSERT_TRUE(cfg->config().messaging.autoTelemetry);
}

void test_auto_telemetry_round_trips() {
    parse("{\"messaging\":{\"auto_telemetry\": false}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_FALSE(cfg->config().messaging.autoTelemetry);
}

// ═══ Share-contact flag (chat-header Share button) ═══

void test_share_contact_defaults_on() {
    parse("{}");
    TEST_ASSERT_TRUE(cfg->config().messaging.shareContact);
}

void test_share_contact_explicit_false() {
    parse("{\"messaging\":{\"share_contact\": false}}");
    TEST_ASSERT_FALSE(cfg->config().messaging.shareContact);
}

void test_share_contact_round_trips() {
    parse("{\"messaging\":{\"share_contact\": false}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_FALSE(cfg->config().messaging.shareContact);
}

// ═══ Display emoji flag ═══

void test_emoji_defaults_true() {
    parse("{\"display\":{}}");
    TEST_ASSERT_TRUE(cfg->config().display.emoji);
}

void test_emoji_explicit_false() {
    parse("{\"display\":{\"emoji\": false}}");
    TEST_ASSERT_FALSE(cfg->config().display.emoji);
}

void test_emoji_round_trips() {
    parse("{\"display\":{\"emoji\": false}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_FALSE(cfg->config().display.emoji);
}

// ═══ Display menu button (T-Deck status-bar gear) ═══

void test_menu_button_defaults_false() {
    parse("{\"display\":{}}");
    TEST_ASSERT_FALSE(cfg->config().display.menuButton);
}

void test_menu_button_explicit_true() {
    parse("{\"display\":{\"menu_button\": true}}");
    TEST_ASSERT_TRUE(cfg->config().display.menuButton);
}

void test_menu_button_round_trips() {
    parse("{\"display\":{\"menu_button\": true}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_TRUE(cfg->config().display.menuButton);
}

// ═══ Debug: screenshots ═══

void test_debug_screenshots_defaults_false() {
    parse("{}");
    TEST_ASSERT_FALSE(cfg->config().debug.screenshots);
}

void test_debug_screenshots_explicit_true() {
    parse("{\"debug\":{\"screenshots\": true}}");
    TEST_ASSERT_TRUE(cfg->config().debug.screenshots);
}

void test_debug_screenshots_round_trips() {
    parse("{\"debug\":{\"screenshots\": true}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_TRUE(cfg->config().debug.screenshots);
}

// ═══ Radio: periodic advert interval ═══

void test_advert_interval_defaults_off() {
    parse("{}");
    TEST_ASSERT_EQUAL_UINT16(0, cfg->config().radio.advertIntervalMin);
}

void test_advert_interval_zero_stays_off() {
    parse("{\"radio\":{\"advert_interval_min\": 0}}");
    TEST_ASSERT_EQUAL_UINT16(0, cfg->config().radio.advertIntervalMin);
}

void test_advert_interval_parsed() {
    parse("{\"radio\":{\"advert_interval_min\": 720}}");
    TEST_ASSERT_EQUAL_UINT16(720, cfg->config().radio.advertIntervalMin);
}

void test_advert_interval_floor_one_hour() {
    parse("{\"radio\":{\"advert_interval_min\": 30}}");
    TEST_ASSERT_EQUAL_UINT16(60, cfg->config().radio.advertIntervalMin);
}

void test_advert_interval_week_cap() {
    parse("{\"radio\":{\"advert_interval_min\": 20000}}");
    TEST_ASSERT_EQUAL_UINT16(10080, cfg->config().radio.advertIntervalMin);
}

void test_advert_interval_round_trips() {
    parse("{\"radio\":{\"advert_interval_min\": 720}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_EQUAL_UINT16(720, cfg->config().radio.advertIntervalMin);
}

// ═══ Location-advert precision ═══

void test_location_precision_defaults_off() {
    parse("{}");
    TEST_ASSERT_EQUAL_UINT8(0, cfg->config().locationPrecision);
}

void test_location_precision_parsed() {
    parse("{\"gps\":{\"location_precision\": 16}}");
    TEST_ASSERT_EQUAL_UINT8(16, cfg->config().locationPrecision);
}

void test_location_precision_clamp_floor() {
    parse("{\"gps\":{\"location_precision\": 5}}");
    TEST_ASSERT_EQUAL_UINT8(10, cfg->config().locationPrecision);
}

void test_location_precision_clamp_cap() {
    parse("{\"gps\":{\"location_precision\": 40}}");
    TEST_ASSERT_EQUAL_UINT8(32, cfg->config().locationPrecision);
}

void test_location_precision_legacy_true() {
    parse("{\"gps\":{\"location_advert\": true}}");
    TEST_ASSERT_EQUAL_UINT8(32, cfg->config().locationPrecision);
}

void test_location_precision_legacy_false() {
    parse("{\"gps\":{\"location_advert\": false}}");
    TEST_ASSERT_EQUAL_UINT8(0, cfg->config().locationPrecision);
}

void test_location_precision_round_trips() {
    parse("{\"gps\":{\"location_precision\": 16}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_EQUAL_UINT8(16, cfg->config().locationPrecision);
}

// ═══ Themes ═══

void test_theme_defaults_dark() {
    parse("{}");
    TEST_ASSERT_EQUAL_STRING("dark", cfg->config().display.theme.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, cfg->config().display.customThemes.size());
}

void test_theme_selected_parsed() {
    parse("{\"display\":{\"theme\":\"amber\"}}");
    TEST_ASSERT_EQUAL_STRING("amber", cfg->config().display.theme.c_str());
}

void test_custom_theme_parsed() {
    parse("{\"display\":{\"theme\":\"mine\",\"themes\":["
          "{\"name\":\"mine\",\"base\":\"light\",\"accent\":\"#FF00AA\",\"bg_primary\":\"#101018\"}]}}");
    auto& ct = cfg->config().display.customThemes;
    TEST_ASSERT_EQUAL_UINT32(1, ct.size());
    TEST_ASSERT_EQUAL_STRING("mine", ct[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("light", ct[0].base.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, ct[0].colors.size());   // accent + bg_primary (name/base excluded)
}

void test_custom_theme_base_defaults_dark() {
    parse("{\"display\":{\"themes\":[{\"name\":\"x\",\"accent\":\"#123456\"}]}}");
    auto& ct = cfg->config().display.customThemes;
    TEST_ASSERT_EQUAL_UINT32(1, ct.size());
    TEST_ASSERT_EQUAL_STRING("dark", ct[0].base.c_str());
}

void test_custom_theme_unnamed_skipped() {
    parse("{\"display\":{\"themes\":[{\"accent\":\"#123456\"},{\"name\":\"ok\"}]}}");
    auto& ct = cfg->config().display.customThemes;
    TEST_ASSERT_EQUAL_UINT32(1, ct.size());
    TEST_ASSERT_EQUAL_STRING("ok", ct[0].name.c_str());
}

void test_custom_theme_round_trips() {
    parse("{\"display\":{\"theme\":\"mine\",\"themes\":["
          "{\"name\":\"mine\",\"base\":\"amber\",\"accent\":\"#FF00AA\"}]}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_EQUAL_STRING("mine", cfg->config().display.theme.c_str());
    auto& ct = cfg->config().display.customThemes;
    TEST_ASSERT_EQUAL_UINT32(1, ct.size());
    TEST_ASSERT_EQUAL_STRING("amber", ct[0].base.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, ct[0].colors.size());
}

// ═══ Radio scope ═══

void test_radio_scope_default_wildcard() {
    parse("{}");
    TEST_ASSERT_EQUAL_STRING("*", cfg->config().radio.scope.c_str());
}

void test_radio_scope_parsed() {
    parse("{\"radio\":{\"scope\": \"#europe\"}}");
    TEST_ASSERT_EQUAL_STRING("#europe", cfg->config().radio.scope.c_str());
}

void test_radio_scope_missing_uses_default() {
    parse("{\"radio\":{\"frequency\": 915.0}}");
    TEST_ASSERT_EQUAL_STRING("*", cfg->config().radio.scope.c_str());
}

void test_channel_scope_default_empty() {
    parse("{\"channels\":[{\"name\":\"#test\",\"type\":\"hashtag\",\"index\":0}]}");
    TEST_ASSERT_EQUAL(1, cfg->config().channels.size());
    TEST_ASSERT_EQUAL_STRING("", cfg->config().channels[0].scope.c_str());
}

void test_channel_scope_parsed() {
    parse("{\"channels\":[{\"name\":\"#test\",\"type\":\"hashtag\",\"index\":0,\"scope\":\"#local\"}]}");
    TEST_ASSERT_EQUAL_STRING("#local", cfg->config().channels[0].scope.c_str());
}

void test_channel_scope_wildcard_override() {
    parse("{\"channels\":[{\"name\":\"#test\",\"type\":\"hashtag\",\"index\":0,\"scope\":\"*\"}]}");
    TEST_ASSERT_EQUAL_STRING("*", cfg->config().channels[0].scope.c_str());
}

void test_radio_scope_round_trips() {
    // Serialize side is conditional (radio scope written only when != "*"); a regression
    // there would silently drop the region on save.
    parse("{\"radio\":{\"scope\":\"#europe\"}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_EQUAL_STRING("#europe", cfg->config().radio.scope.c_str());
}

void test_radio_scope_wildcard_round_trips() {
    // The "*" default must survive the round trip even though it isn't serialized.
    parse("{\"radio\":{\"scope\":\"*\"}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_EQUAL_STRING("*", cfg->config().radio.scope.c_str());
}

void test_channel_scope_round_trips() {
    // channel scope is written only when non-empty; an empty (inherit) scope must also survive.
    parse("{\"channels\":[{\"name\":\"#a\",\"type\":\"hashtag\",\"index\":0,\"scope\":\"#local\"},"
          "{\"name\":\"#b\",\"type\":\"hashtag\",\"index\":1}]}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_EQUAL_STRING("#local", cfg->config().channels[0].scope.c_str());
    TEST_ASSERT_EQUAL_STRING("", cfg->config().channels[1].scope.c_str());
}

// ═══ Radio path-hash mode (bytes per repeater hop) ═══

void test_path_hash_mode_parsed() {
    parse("{\"radio\":{\"path_hash_mode\": 2}}");
    TEST_ASSERT_EQUAL_UINT8(2, cfg->config().radio.pathHashMode);
}

void test_path_hash_mode_default() {
    parse("{}");
    TEST_ASSERT_EQUAL_UINT8(defaults::RADIO_PATH_HASH_MODE, cfg->config().radio.pathHashMode);
}

void test_path_hash_mode_clamped_high() {
    // Out-of-range (>2) must fall back to the default, not store a bogus byte-count.
    parse("{\"radio\":{\"path_hash_mode\": 5}}");
    TEST_ASSERT_EQUAL_UINT8(defaults::RADIO_PATH_HASH_MODE, cfg->config().radio.pathHashMode);
}

void test_path_hash_mode_round_trips() {
    parse("{\"radio\":{\"path_hash_mode\": 1}}");
    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    cfg->parseJson(json);
    TEST_ASSERT_EQUAL_UINT8(1, cfg->config().radio.pathHashMode);
}

// ═══ Missing sections use defaults ═══

void test_missing_radio_uses_defaults() {
    parse("{}");
    TEST_ASSERT_EQUAL_FLOAT(defaults::RADIO_FREQUENCY, cfg->config().radio.frequency);
    TEST_ASSERT_EQUAL_UINT8(defaults::RADIO_SPREADING_FACTOR, cfg->config().radio.spreadingFactor);
}

void test_missing_display_uses_defaults() {
    parse("{}");
    TEST_ASSERT_EQUAL_UINT8(defaults::DISPLAY_BRIGHTNESS, cfg->config().display.brightness);
}

void test_missing_gps_uses_defaults() {
    parse("{}");
    TEST_ASSERT_TRUE(cfg->config().gpsEnabled);
    TEST_ASSERT_EQUAL_INT8(0, cfg->config().gpsClockOffset);
}

// ═══ Unified Lock Config ═══

void test_lock_defaults() {
    parse("{}");
    // No lock fields at all — uses new defaults
    TEST_ASSERT_EQUAL_STRING("key", cfg->config().security.lockMode.c_str());
    TEST_ASSERT_EQUAL_STRING("key", cfg->config().security.autoLock.c_str());
}

void test_lock_mode_pin() {
    parse("{\"security\":{\"lock\":\"pin\",\"pin_code\":\"1234\"}}");
    TEST_ASSERT_EQUAL_STRING("pin", cfg->config().security.lockMode.c_str());
    TEST_ASSERT_EQUAL_STRING("1234", cfg->config().security.pinCode.c_str());
}

void test_lock_mode_none() {
    parse("{\"security\":{\"lock\":\"none\"}}");
    TEST_ASSERT_EQUAL_STRING("none", cfg->config().security.lockMode.c_str());
}

void test_lock_invalid_falls_back() {
    parse("{\"security\":{\"lock\":\"invalid\"}}");
    TEST_ASSERT_EQUAL_STRING("none", cfg->config().security.lockMode.c_str());
}

void test_lock_backwards_compat_pin() {
    parse("{\"security\":{\"pin_enabled\":true,\"pin_code\":\"abcd\"}}");
    TEST_ASSERT_EQUAL_STRING("pin", cfg->config().security.lockMode.c_str());
    TEST_ASSERT_EQUAL_STRING("pin", cfg->config().security.autoLock.c_str());
    TEST_ASSERT_EQUAL_STRING("abcd", cfg->config().security.pinCode.c_str());
}

void test_lock_backwards_compat_key() {
    parse("{\"security\":{\"key_lock\":true,\"auto_key_lock\":true}}");
    TEST_ASSERT_EQUAL_STRING("key", cfg->config().security.lockMode.c_str());
    TEST_ASSERT_EQUAL_STRING("key", cfg->config().security.autoLock.c_str());
}

void test_lock_backwards_compat_none() {
    parse("{\"security\":{\"key_lock\":false,\"pin_enabled\":false,\"auto_key_lock\":false}}");
    TEST_ASSERT_EQUAL_STRING("none", cfg->config().security.lockMode.c_str());
    TEST_ASSERT_EQUAL_STRING("none", cfg->config().security.autoLock.c_str());
}

void test_lock_backwards_compat_missing_auto() {
    // Old config without auto_key_lock field — should use new default "key"
    parse("{\"security\":{\"key_lock\":true}}");
    TEST_ASSERT_EQUAL_STRING("key", cfg->config().security.lockMode.c_str());
    TEST_ASSERT_EQUAL_STRING("key", cfg->config().security.autoLock.c_str());
}

void test_auto_lock_values() {
    parse("{\"security\":{\"lock\":\"pin\",\"auto_lock\":\"key\"}}");
    TEST_ASSERT_EQUAL_STRING("key", cfg->config().security.autoLock.c_str());
}

void test_auto_lock_invalid_falls_back() {
    parse("{\"security\":{\"auto_lock\":\"bogus\"}}");
    TEST_ASSERT_EQUAL_STRING("none", cfg->config().security.autoLock.c_str());
}

// ═══ from_discovery flag ═══

void test_from_discovery_round_trip_true() {
    parse("{\"contacts\":[{\"alias\":\"d\",\"public_key\":\"abcd\",\"from_discovery\":true}]}");
    TEST_ASSERT_EQUAL_INT(1, cfg->config().contacts.size());
    TEST_ASSERT_TRUE(cfg->config().contacts[0].fromDiscovery);

    String json = cfg->toJson();
    cfg->config() = AppConfig{};
    parse(json.c_str());
    TEST_ASSERT_EQUAL_INT(1, cfg->config().contacts.size());
    TEST_ASSERT_TRUE(cfg->config().contacts[0].fromDiscovery);
}

void test_from_discovery_default_false() {
    parse("{\"contacts\":[{\"alias\":\"a\",\"public_key\":\"abcd\"}]}");
    TEST_ASSERT_EQUAL_INT(1, cfg->config().contacts.size());
    TEST_ASSERT_FALSE(cfg->config().contacts[0].fromDiscovery);
}

// ═══ ConfigManager::load() bak fallback ═══

void test_load_uses_main_when_valid() {
    setStubFile(defaults::CONFIG_PATH, "{\"device\":{\"name\":\"Main\"}}");
    setStubFile((String(defaults::CONFIG_PATH) + ".bak").c_str(),
                "{\"device\":{\"name\":\"Bak\"}}");
    ConfigManager::LoadResult r = cfg->load();
    TEST_ASSERT_EQUAL_INT(ConfigManager::LOAD_OK, r);
    TEST_ASSERT_EQUAL_STRING("Main", cfg->config().deviceName.c_str());
}

void test_load_falls_back_to_bak_on_corrupt_main() {
    setStubFile(defaults::CONFIG_PATH, "{broken json!!!");
    String bakPath = String(defaults::CONFIG_PATH) + ".bak";
    setStubFile(bakPath.c_str(), "{\"device\":{\"name\":\"Bak\"}}");
    ConfigManager::LoadResult r = cfg->load();
    TEST_ASSERT_EQUAL_INT(ConfigManager::LOAD_OK, r);
    TEST_ASSERT_EQUAL_STRING("Bak", cfg->config().deviceName.c_str());
    // Recovery should promote bak content to main and remove bak so the
    // next save's writeAtomic doesn't rotate the corrupt main back into
    // .bak (destroying the recovery copy).
    TEST_ASSERT_TRUE(getStubFile(defaults::CONFIG_PATH).indexOf("Bak") >= 0);
    TEST_ASSERT_TRUE(getStubFile(bakPath.c_str()).length() == 0);
}

void test_load_falls_back_to_bak_on_empty_main() {
    setStubFile(defaults::CONFIG_PATH, "");
    setStubFile((String(defaults::CONFIG_PATH) + ".bak").c_str(),
                "{\"device\":{\"name\":\"Bak\"}}");
    ConfigManager::LoadResult r = cfg->load();
    TEST_ASSERT_EQUAL_INT(ConfigManager::LOAD_OK, r);
    TEST_ASSERT_EQUAL_STRING("Bak", cfg->config().deviceName.c_str());
}

void test_load_returns_error_when_both_corrupt() {
    setStubFile(defaults::CONFIG_PATH, "{broken");
    setStubFile((String(defaults::CONFIG_PATH) + ".bak").c_str(), "also broken");
    ConfigManager::LoadResult r = cfg->load();
    TEST_ASSERT_EQUAL_INT(ConfigManager::LOAD_ERROR, r);
}

void test_load_no_file_when_neither_exists() {
    ConfigManager::LoadResult r = cfg->load();
    TEST_ASSERT_EQUAL_INT(ConfigManager::LOAD_NO_FILE, r);
}

void test_load_recovers_from_bak_when_main_missing() {
    // Mid-save power-loss leaves us with no main file but a valid bak.
    // Old behaviour was LOAD_NO_FILE → first-boot regen → identity loss.
    // New behaviour recovers the prior state from .bak and promotes it
    // back to main, dropping the now-redundant .bak.
    String bakPath = String(defaults::CONFIG_PATH) + ".bak";
    setStubFile(bakPath.c_str(), "{\"device\":{\"name\":\"Recovered\"}}");
    ConfigManager::LoadResult r = cfg->load();
    TEST_ASSERT_EQUAL_INT(ConfigManager::LOAD_OK, r);
    TEST_ASSERT_EQUAL_STRING("Recovered", cfg->config().deviceName.c_str());
    TEST_ASSERT_TRUE(getStubFile(defaults::CONFIG_PATH).indexOf("Recovered") >= 0);
    TEST_ASSERT_TRUE(getStubFile(bakPath.c_str()).length() == 0);
}

void test_load_error_when_main_missing_and_bak_corrupt() {
    setStubFile((String(defaults::CONFIG_PATH) + ".bak").c_str(), "{garbage");
    ConfigManager::LoadResult r = cfg->load();
    TEST_ASSERT_EQUAL_INT(ConfigManager::LOAD_ERROR, r);
}

// ═══ Save path sanity ═══

void test_save_calls_writeatomic() {
    cfg->config().deviceName = "savetest";
    bool ok = cfg->save();
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING(defaults::CONFIG_PATH, g_last_atomic_path.c_str());
    TEST_ASSERT_TRUE(g_last_atomic_content.find("savetest") != std::string::npos);
}

// ═══ appendDiscoveredContact ═══

void test_append_discovered_contact_succeeds() {
    ContactConfig cc;
    cc.alias         = "node_01";
    cc.publicKey     = "deadbeef";
    cc.fromDiscovery = true;
    TEST_ASSERT_TRUE(cfg->appendDiscoveredContact(cc));
    TEST_ASSERT_EQUAL_INT(1, cfg->config().contacts.size());
    // Saved through writeAtomic; emitted JSON contains the new contact.
    TEST_ASSERT_TRUE(g_last_atomic_content.find("deadbeef") != std::string::npos);
    TEST_ASSERT_TRUE(g_last_atomic_content.find("\"from_discovery\": true") != std::string::npos);
}

void test_append_discovered_contact_refuses_duplicate() {
    ContactConfig cc;
    cc.alias = "first"; cc.publicKey = "aabb";
    cfg->config().contacts.push_back(cc);

    ContactConfig dup;
    dup.alias = "second"; dup.publicKey = "aabb"; dup.fromDiscovery = true;
    TEST_ASSERT_FALSE(cfg->appendDiscoveredContact(dup));
    TEST_ASSERT_EQUAL_INT(1, cfg->config().contacts.size());
}

void test_append_discovered_contact_refuses_at_cap() {
    for (int i = 0; i < defaults::MAX_CHAT_CONTACTS; i++) {
        ContactConfig cc;
        cc.alias     = String("filler_") + String(i);
        cc.publicKey = String("k") + String(i);
        cfg->config().contacts.push_back(cc);
    }
    TEST_ASSERT_EQUAL_INT(defaults::MAX_CHAT_CONTACTS, (int)cfg->config().contacts.size());

    ContactConfig overflow;
    overflow.alias = "overflow"; overflow.publicKey = "newkey";
    TEST_ASSERT_FALSE(cfg->appendDiscoveredContact(overflow));
    TEST_ASSERT_EQUAL_INT(defaults::MAX_CHAT_CONTACTS, (int)cfg->config().contacts.size());
}

// ═══ companion contact resolution + alias edit (#33) ═══

static void makeKey(uint8_t* k, uint8_t offset) {
    for (int i = 0; i < 32; i++) k[i] = (uint8_t)(i + offset);
}
static String keyToHex(const uint8_t* k) {
    char h[65];
    for (int i = 0; i < 32; i++) sprintf(h + i * 2, "%02x", k[i]);
    h[64] = '\0';
    return String(h);
}
static String keyToB64(const uint8_t* k) {
    unsigned char out[64]; size_t olen = 0;
    mbedtls_base64_encode(out, sizeof(out), &olen, k, 32);
    return String((const char*)out);
}

void test_find_contact_index_by_key_hex() {
    uint8_t k0[32], k1[32]; makeKey(k0, 0); makeKey(k1, 100);
    ContactConfig a; a.alias = "a"; a.publicKey = keyToHex(k0);
    ContactConfig b; b.alias = "b"; b.publicKey = keyToHex(k1);
    cfg->config().contacts.push_back(a);
    cfg->config().contacts.push_back(b);
    TEST_ASSERT_EQUAL_INT(0, cfg->findContactIndexByKey(k0));
    TEST_ASSERT_EQUAL_INT(1, cfg->findContactIndexByKey(k1));
}

void test_find_contact_index_by_key_base64() {
    // Key stored as base64 (config-tool format) must still resolve from raw bytes.
    uint8_t k0[32], k1[32]; makeKey(k0, 0); makeKey(k1, 100);
    ContactConfig a; a.alias = "a"; a.publicKey = keyToB64(k0);
    cfg->config().contacts.push_back(a);
    TEST_ASSERT_EQUAL_INT(0, cfg->findContactIndexByKey(k0));
    TEST_ASSERT_EQUAL_INT(-1, cfg->findContactIndexByKey(k1));   // unknown -> -1
}

void test_find_contact_index_mixed_formats() {
    // One contact stored hex, another base64 — both resolve (the audit-found bug).
    uint8_t k0[32], k1[32]; makeKey(k0, 0); makeKey(k1, 100);
    ContactConfig a; a.alias = "hex"; a.publicKey = keyToHex(k0);
    ContactConfig b; b.alias = "b64"; b.publicKey = keyToB64(k1);
    cfg->config().contacts.push_back(a);
    cfg->config().contacts.push_back(b);
    TEST_ASSERT_EQUAL_INT(0, cfg->findContactIndexByKey(k0));
    TEST_ASSERT_EQUAL_INT(1, cfg->findContactIndexByKey(k1));
}

void test_update_contact_alias() {
    uint8_t k[32]; makeKey(k, 5);
    ContactConfig a; a.alias = "old"; a.publicKey = keyToHex(k);
    cfg->config().contacts.push_back(a);
    TEST_ASSERT_TRUE(cfg->updateContactAlias(0, "renamed"));
    TEST_ASSERT_EQUAL_STRING("renamed", cfg->config().contacts[0].alias.c_str());
    TEST_ASSERT_TRUE(g_last_atomic_content.find("renamed") != std::string::npos);  // persisted
    TEST_ASSERT_FALSE(cfg->updateContactAlias(5, "x"));   // bad index
}

// ═══ channel / room add + remove ═══

void test_append_channel_hashtag_assigns_index() {
    ChannelConfig a; a.name = "#alpha"; a.type = "hashtag";
    ChannelConfig b; b.name = "#beta";  b.type = "hashtag";
    TEST_ASSERT_TRUE(cfg->appendChannel(a));
    TEST_ASSERT_TRUE(cfg->appendChannel(b));
    TEST_ASSERT_EQUAL_INT(2, (int)cfg->config().channels.size());
    TEST_ASSERT_EQUAL_UINT8(0, cfg->config().channels[0].index);
    TEST_ASSERT_EQUAL_UINT8(1, cfg->config().channels[1].index);
}

void test_append_channel_public_only_one() {
    ChannelConfig p; p.type = "public";
    TEST_ASSERT_TRUE(cfg->appendChannel(p));
    TEST_ASSERT_EQUAL_STRING("Public", cfg->config().channels[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("8b3387e9c5cdea6ac9e5edbaa115cd72", cfg->config().channels[0].psk.c_str());
    ChannelConfig p2; p2.type = "public";
    TEST_ASSERT_FALSE(cfg->appendChannel(p2));  // refuse second public
    TEST_ASSERT_EQUAL_INT(1, (int)cfg->config().channels.size());
}

void test_append_channel_private_requires_psk() {
    ChannelConfig bad; bad.name = "priv"; bad.type = "private"; bad.psk = "xyz";
    TEST_ASSERT_FALSE(cfg->appendChannel(bad));
    ChannelConfig ok; ok.name = "priv"; ok.type = "private";
    ok.psk = "0123456789abcdef0123456789abcdef";  // 32 hex
    TEST_ASSERT_TRUE(cfg->appendChannel(ok));
    TEST_ASSERT_EQUAL_INT(1, (int)cfg->config().channels.size());
}

void test_append_channel_refuses_duplicate_name() {
    ChannelConfig a; a.name = "#dup"; a.type = "hashtag";
    TEST_ASSERT_TRUE(cfg->appendChannel(a));
    ChannelConfig b; b.name = "#DUP"; b.type = "hashtag";  // case-insensitive clash
    TEST_ASSERT_FALSE(cfg->appendChannel(b));
    TEST_ASSERT_EQUAL_INT(1, (int)cfg->config().channels.size());
}

void test_append_channel_refuses_at_cap() {
    for (int i = 0; i < defaults::MAX_CHANNELS; i++) {
        ChannelConfig c; c.name = "#c" + String(i); c.type = "hashtag";
        TEST_ASSERT_TRUE(cfg->appendChannel(c));
    }
    ChannelConfig overflow; overflow.name = "#over"; overflow.type = "hashtag";
    TEST_ASSERT_FALSE(cfg->appendChannel(overflow));
    TEST_ASSERT_EQUAL_INT(defaults::MAX_CHANNELS, (int)cfg->config().channels.size());
}

void test_remove_channel_reindexes() {
    for (int i = 0; i < 3; i++) {
        ChannelConfig c; c.name = "#c" + String(i); c.type = "hashtag";
        cfg->appendChannel(c);
    }
    TEST_ASSERT_TRUE(cfg->removeChannelAt(0));  // drop #c0
    TEST_ASSERT_EQUAL_INT(2, (int)cfg->config().channels.size());
    TEST_ASSERT_EQUAL_STRING("#c1", cfg->config().channels[0].name.c_str());
    TEST_ASSERT_EQUAL_UINT8(0, cfg->config().channels[0].index);  // re-indexed
    TEST_ASSERT_EQUAL_UINT8(1, cfg->config().channels[1].index);
    TEST_ASSERT_FALSE(cfg->removeChannelAt(9));  // out of range
}

void test_append_room_dup_pubkey_refused() {
    RoomServerConfig r; r.name = "room1";
    r.publicKey = "AA11223344556677889900112233445566778899001122334455667788990011";  // 64 hex
    TEST_ASSERT_TRUE(cfg->appendRoom(r));
    TEST_ASSERT_EQUAL_INT(1, (int)cfg->config().roomServers.size());
    // stored lowercased
    TEST_ASSERT_EQUAL_STRING("aa11223344556677889900112233445566778899001122334455667788990011",
                             cfg->config().roomServers[0].publicKey.c_str());
    RoomServerConfig dup; dup.name = "room2"; dup.publicKey = r.publicKey;  // same key (upper)
    TEST_ASSERT_FALSE(cfg->appendRoom(dup));
    RoomServerConfig bad; bad.name = "bad"; bad.publicKey = "tooshort";
    TEST_ASSERT_FALSE(cfg->appendRoom(bad));
    TEST_ASSERT_EQUAL_INT(1, (int)cfg->config().roomServers.size());
}

void test_append_room_refuses_at_cap() {
    for (int i = 0; i < defaults::MAX_ROOM_SERVERS; i++) {
        RoomServerConfig r; r.name = "r" + String(i);
        char k[65]; for (int j = 0; j < 64; j++) k[j] = "0123456789abcdef"[(i * 7 + j) % 16]; k[64] = 0;
        r.publicKey = String(k);
        TEST_ASSERT_TRUE(cfg->appendRoom(r));
    }
    RoomServerConfig over; over.name = "over";
    over.publicKey = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    TEST_ASSERT_FALSE(cfg->appendRoom(over));
    TEST_ASSERT_EQUAL_INT(defaults::MAX_ROOM_SERVERS, (int)cfg->config().roomServers.size());
}

void test_remove_contact_and_room() {
    ContactConfig c; c.alias = "x"; c.publicKey = "abcd"; TEST_ASSERT_TRUE(cfg->appendDiscoveredContact(c));
    TEST_ASSERT_TRUE(cfg->removeContactAt(0));
    TEST_ASSERT_EQUAL_INT(0, (int)cfg->config().contacts.size());
    TEST_ASSERT_FALSE(cfg->removeContactAt(0));  // empty now
    RoomServerConfig r; r.name = "room"; r.publicKey =
        "0011223344556677889900112233445566778899001122334455667788990011";
    TEST_ASSERT_TRUE(cfg->appendRoom(r));
    TEST_ASSERT_TRUE(cfg->removeRoomAt(0));
    TEST_ASSERT_EQUAL_INT(0, (int)cfg->config().roomServers.size());
}

// ── Scope space sanitization (#36): a scope is one region name, never a list ──
void test_sanitize_scope_cuts_at_first_space() {
    String s = "west pnw or wv eug";
    TEST_ASSERT_TRUE(sanitizeScope(s));            // reports it changed
    TEST_ASSERT_EQUAL_STRING("west", s.c_str());   // kept only the first token
}
void test_sanitize_scope_single_token_unchanged() {
    String s = "eug";
    TEST_ASSERT_FALSE(sanitizeScope(s));
    TEST_ASSERT_EQUAL_STRING("eug", s.c_str());
}
void test_sanitize_scope_trims_but_keeps_wildcard() {
    String s = "  eug  ";
    sanitizeScope(s);
    TEST_ASSERT_EQUAL_STRING("eug", s.c_str());
    String w = "*";
    TEST_ASSERT_FALSE(sanitizeScope(w));
    TEST_ASSERT_EQUAL_STRING("*", w.c_str());
}
void test_config_load_cuts_scope_at_space() {
    parse("{\"radio\":{\"scope\":\"west pnw or wv eug\"}}");
    TEST_ASSERT_EQUAL_STRING("west", cfg->config().radio.scope.c_str());
}

int main() {
    UNITY_BEGIN();

    // Invalid JSON
    RUN_TEST(test_invalid_json_rejected);
    RUN_TEST(test_empty_json_accepted);

    // Radio bounds
    RUN_TEST(test_radio_frequency_too_low);
    RUN_TEST(test_radio_frequency_too_high);
    RUN_TEST(test_radio_frequency_valid);
    RUN_TEST(test_radio_sf_too_low);
    RUN_TEST(test_radio_sf_too_high);
    RUN_TEST(test_radio_sf_valid);
    RUN_TEST(test_radio_tx_power_too_high);
    RUN_TEST(test_radio_tx_power_too_low);
    RUN_TEST(test_radio_coding_rate_bounds);
    RUN_TEST(test_radio_bandwidth_bounds);

    // Device name
    RUN_TEST(test_device_name_truncated_at_20);
    RUN_TEST(test_device_name_normal);

    // Constrained fields
    RUN_TEST(test_max_retries_clamped_low);
    RUN_TEST(test_max_retries_clamped_high);
    RUN_TEST(test_max_retries_valid);
    RUN_TEST(test_sos_repeat_clamped);
    RUN_TEST(test_gps_clock_offset_clamped);
    RUN_TEST(test_gps_last_known_max_age_clamped);
    RUN_TEST(test_battery_threshold_clamped);
    RUN_TEST(test_kbd_brightness_clamped);

    // Enum validation
    RUN_TEST(test_show_telemetry_valid_values);
    RUN_TEST(test_show_telemetry_invalid_falls_back);

    // Contacts
    RUN_TEST(test_contact_empty_key_skipped);
    RUN_TEST(test_contact_with_key_accepted);
    RUN_TEST(test_contact_defaults);

    // Channels
    RUN_TEST(test_channel_duplicate_index_skipped);
    RUN_TEST(test_private_channel_without_psk_skipped);
    RUN_TEST(test_hashtag_channel_without_psk_accepted);
    RUN_TEST(test_channel_send_sos_type_aware_default);
    RUN_TEST(test_channel_send_sos_explicit_overrides_default);

    // Canned messages
    RUN_TEST(test_canned_messages_bool_true);
    RUN_TEST(test_canned_messages_bool_false);
    RUN_TEST(test_canned_messages_array_enables_and_stores);
    RUN_TEST(test_canned_messages_array_max_8);
    RUN_TEST(test_contact_canned_parsed);
    RUN_TEST(test_channel_canned_parsed);
    RUN_TEST(test_room_canned_parsed);
    RUN_TEST(test_contact_canned_absent_is_empty);
    RUN_TEST(test_canned_blanks_skipped_and_capped);
    RUN_TEST(test_canned_global_array_roundtrips);
    RUN_TEST(test_canned_global_off_roundtrips);
    RUN_TEST(test_canned_array_then_bool_false_clears);
    RUN_TEST(test_canned_array_then_absent_key_clears);
    RUN_TEST(test_canned_roundtrips_through_serialize);
    RUN_TEST(test_empty_canned_omitted_from_serialize);
    RUN_TEST(test_auto_telemetry_defaults_off);
    RUN_TEST(test_auto_telemetry_explicit_true);
    RUN_TEST(test_auto_telemetry_round_trips);
    RUN_TEST(test_share_contact_defaults_on);
    RUN_TEST(test_share_contact_explicit_false);
    RUN_TEST(test_share_contact_round_trips);
    RUN_TEST(test_emoji_defaults_true);
    RUN_TEST(test_emoji_explicit_false);
    RUN_TEST(test_emoji_round_trips);
    RUN_TEST(test_menu_button_defaults_false);
    RUN_TEST(test_menu_button_explicit_true);
    RUN_TEST(test_menu_button_round_trips);
    RUN_TEST(test_debug_screenshots_defaults_false);
    RUN_TEST(test_debug_screenshots_explicit_true);
    RUN_TEST(test_debug_screenshots_round_trips);
    RUN_TEST(test_advert_interval_defaults_off);
    RUN_TEST(test_advert_interval_zero_stays_off);
    RUN_TEST(test_advert_interval_parsed);
    RUN_TEST(test_advert_interval_floor_one_hour);
    RUN_TEST(test_advert_interval_week_cap);
    RUN_TEST(test_advert_interval_round_trips);
    RUN_TEST(test_location_precision_defaults_off);
    RUN_TEST(test_location_precision_parsed);
    RUN_TEST(test_location_precision_clamp_floor);
    RUN_TEST(test_location_precision_clamp_cap);
    RUN_TEST(test_location_precision_legacy_true);
    RUN_TEST(test_location_precision_legacy_false);
    RUN_TEST(test_location_precision_round_trips);
    RUN_TEST(test_theme_defaults_dark);
    RUN_TEST(test_theme_selected_parsed);
    RUN_TEST(test_custom_theme_parsed);
    RUN_TEST(test_custom_theme_base_defaults_dark);
    RUN_TEST(test_custom_theme_unnamed_skipped);
    RUN_TEST(test_custom_theme_round_trips);

    // Radio scope
    RUN_TEST(test_radio_scope_default_wildcard);
    RUN_TEST(test_radio_scope_parsed);
    RUN_TEST(test_radio_scope_missing_uses_default);
    RUN_TEST(test_channel_scope_default_empty);
    RUN_TEST(test_channel_scope_parsed);
    RUN_TEST(test_channel_scope_wildcard_override);
    RUN_TEST(test_radio_scope_round_trips);
    RUN_TEST(test_radio_scope_wildcard_round_trips);
    RUN_TEST(test_channel_scope_round_trips);
    RUN_TEST(test_path_hash_mode_parsed);
    RUN_TEST(test_path_hash_mode_default);
    RUN_TEST(test_path_hash_mode_clamped_high);
    RUN_TEST(test_path_hash_mode_round_trips);

    // Missing sections
    RUN_TEST(test_missing_radio_uses_defaults);
    RUN_TEST(test_missing_display_uses_defaults);
    RUN_TEST(test_missing_gps_uses_defaults);

    // Unified lock config
    RUN_TEST(test_lock_defaults);
    RUN_TEST(test_lock_mode_pin);
    RUN_TEST(test_lock_mode_none);
    RUN_TEST(test_lock_invalid_falls_back);
    RUN_TEST(test_lock_backwards_compat_pin);
    RUN_TEST(test_lock_backwards_compat_key);
    RUN_TEST(test_lock_backwards_compat_none);
    RUN_TEST(test_lock_backwards_compat_missing_auto);
    RUN_TEST(test_auto_lock_values);
    RUN_TEST(test_auto_lock_invalid_falls_back);

    // from_discovery
    RUN_TEST(test_from_discovery_round_trip_true);
    RUN_TEST(test_from_discovery_default_false);

    // load() bak fallback
    RUN_TEST(test_load_uses_main_when_valid);
    RUN_TEST(test_load_falls_back_to_bak_on_corrupt_main);
    RUN_TEST(test_load_falls_back_to_bak_on_empty_main);
    RUN_TEST(test_load_returns_error_when_both_corrupt);
    RUN_TEST(test_load_no_file_when_neither_exists);
    RUN_TEST(test_load_recovers_from_bak_when_main_missing);
    RUN_TEST(test_load_error_when_main_missing_and_bak_corrupt);

    // Save path sanity
    RUN_TEST(test_save_calls_writeatomic);

    // permissions
    RUN_TEST(test_permissions_default_full);
    RUN_TEST(test_permissions_round_trip);
    RUN_TEST(test_permissions_none_mode);
    RUN_TEST(test_permissions_invalid_settings_falls_back);

    // appendDiscoveredContact
    RUN_TEST(test_append_discovered_contact_succeeds);
    RUN_TEST(test_append_discovered_contact_refuses_duplicate);
    RUN_TEST(test_append_discovered_contact_refuses_at_cap);
    RUN_TEST(test_find_contact_index_by_key_hex);
    RUN_TEST(test_find_contact_index_by_key_base64);
    RUN_TEST(test_find_contact_index_mixed_formats);
    RUN_TEST(test_update_contact_alias);

    // channel/room add + remove
    RUN_TEST(test_append_channel_hashtag_assigns_index);
    RUN_TEST(test_append_channel_public_only_one);
    RUN_TEST(test_append_channel_private_requires_psk);
    RUN_TEST(test_append_channel_refuses_duplicate_name);
    RUN_TEST(test_append_channel_refuses_at_cap);
    RUN_TEST(test_remove_channel_reindexes);
    RUN_TEST(test_append_room_dup_pubkey_refused);
    RUN_TEST(test_append_room_refuses_at_cap);
    RUN_TEST(test_remove_contact_and_room);

    // Scope space sanitization (#36)
    RUN_TEST(test_sanitize_scope_cuts_at_first_space);
    RUN_TEST(test_sanitize_scope_single_token_unchanged);
    RUN_TEST(test_sanitize_scope_trims_but_keeps_wildcard);
    RUN_TEST(test_config_load_cuts_scope_at_space);

    return UNITY_END();
}
