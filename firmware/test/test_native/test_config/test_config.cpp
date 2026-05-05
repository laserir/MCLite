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

    // appendDiscoveredContact
    RUN_TEST(test_append_discovered_contact_succeeds);
    RUN_TEST(test_append_discovered_contact_refuses_duplicate);
    RUN_TEST(test_append_discovered_contact_refuses_at_cap);

    return UNITY_END();
}
