#include <Arduino.h>

// Make parseJson accessible
#define private public
#include "config/ConfigManager.h"
#undef private

#include "config/defaults.h"

// Pull in ConfigManager implementation
#include "config/ConfigManager.cpp"

// Stub SDCard methods (load/save/generate call these, but we only test parseJson)
namespace mclite {
    SDCard& SDCard::instance() { static SDCard inst; return inst; }
    bool SDCard::fileExists(const char*) { return false; }
    String SDCard::readFile(const char*, size_t) { return ""; }
    bool SDCard::writeFile(const char*, const String&) { return false; }
}

#include <unity.h>

using namespace mclite;

static ConfigManager* cfg;

void setUp() {
    cfg = &ConfigManager::instance();
    // Reset to defaults before each test
    cfg->config() = AppConfig{};
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

    // Canned messages
    RUN_TEST(test_canned_messages_bool_true);
    RUN_TEST(test_canned_messages_bool_false);
    RUN_TEST(test_canned_messages_array_enables_and_stores);
    RUN_TEST(test_canned_messages_array_max_8);

    // Radio scope
    RUN_TEST(test_radio_scope_default_wildcard);
    RUN_TEST(test_radio_scope_parsed);
    RUN_TEST(test_radio_scope_missing_uses_default);
    RUN_TEST(test_channel_scope_default_empty);
    RUN_TEST(test_channel_scope_parsed);
    RUN_TEST(test_channel_scope_wildcard_override);

    // Missing sections
    RUN_TEST(test_missing_radio_uses_defaults);
    RUN_TEST(test_missing_display_uses_defaults);
    RUN_TEST(test_missing_gps_uses_defaults);

    return UNITY_END();
}
