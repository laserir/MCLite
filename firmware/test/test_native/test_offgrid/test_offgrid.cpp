#include <Arduino.h>
#include <unity.h>

#include "util/offgrid.h"

#define private public
#include "config/ConfigManager.h"
#undef private
#include "config/defaults.h"
#include "config/ConfigManager.cpp"
#include "config/offgrid_presets.h"

namespace mclite {
    SDCard& SDCard::instance() { static SDCard inst; return inst; }
    bool SDCard::fileExists(const char*) { return false; }
    String SDCard::readFile(const char*, size_t) { return ""; }
    bool SDCard::writeFile(const char*, const String&) { return false; }
    bool SDCard::writeAtomic(const char*, const String&) { return false; }
    bool SDCard::remove(const char*) { return false; }
}

using namespace mclite;

void setUp() {
    ConfigManager::instance().config() = AppConfig{};
}
void tearDown() {}

// ═══ offgridFreqFor(): band-boundary table ═══

void test_offgrid_433_lower_edge() {
    TEST_ASSERT_EQUAL_FLOAT(433.0f, offgridFreqFor(433.0f));
}
void test_offgrid_500_maps_to_433() {
    TEST_ASSERT_EQUAL_FLOAT(433.0f, offgridFreqFor(500.0f));
}
void test_offgrid_599_just_below_869_cut() {
    TEST_ASSERT_EQUAL_FLOAT(433.0f, offgridFreqFor(599.9f));
}
void test_offgrid_600_crosses_to_869() {
    TEST_ASSERT_EQUAL_FLOAT(869.0f, offgridFreqFor(600.0f));
}
void test_offgrid_eu_narrow_default() {
    TEST_ASSERT_EQUAL_FLOAT(869.0f, offgridFreqFor(869.525f));
}
void test_offgrid_eu_wide_default() {
    TEST_ASSERT_EQUAL_FLOAT(869.0f, offgridFreqFor(869.618f));
}
void test_offgrid_893_9_still_869() {
    TEST_ASSERT_EQUAL_FLOAT(869.0f, offgridFreqFor(893.9f));
}
void test_offgrid_894_crosses_to_918() {
    TEST_ASSERT_EQUAL_FLOAT(918.0f, offgridFreqFor(894.0f));
}
void test_offgrid_us_default() {
    TEST_ASSERT_EQUAL_FLOAT(918.0f, offgridFreqFor(910.525f));
}
void test_offgrid_918_itself() {
    TEST_ASSERT_EQUAL_FLOAT(918.0f, offgridFreqFor(918.0f));
}
void test_offgrid_960_upper() {
    TEST_ASSERT_EQUAL_FLOAT(918.0f, offgridFreqFor(960.0f));
}

// ═══ Config parse: offgrid block round-trip ═══

void test_offgrid_missing_block_defaults_false() {
    // Backwards compat: old configs have no offgrid block
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(String("{}")));
    TEST_ASSERT_FALSE(ConfigManager::instance().config().offgrid.enabled);
}

void test_offgrid_enabled_true_parsed() {
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(
        String("{\"offgrid\":{\"enabled\":true}}")));
    TEST_ASSERT_TRUE(ConfigManager::instance().config().offgrid.enabled);
}

void test_offgrid_enabled_false_parsed() {
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(
        String("{\"offgrid\":{\"enabled\":false}}")));
    TEST_ASSERT_FALSE(ConfigManager::instance().config().offgrid.enabled);
}

void test_offgrid_serialized_round_trip() {
    ConfigManager::instance().config().offgrid.enabled = true;
    String json = ConfigManager::instance().toJson();
    // Reset then reparse our own output
    ConfigManager::instance().config() = AppConfig{};
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(json));
    TEST_ASSERT_TRUE(ConfigManager::instance().config().offgrid.enabled);
}

// ═══ resolveOffgrid(): what actually goes on the air (GitHub #49) ═══
//
// The contract that matters most is the first one: "auto" must reproduce the
// pre-preset behaviour exactly, or upgrading silently moves people's radios.

void test_resolve_auto_is_the_old_behaviour() {
    AppConfig c;                       // defaults: EU/UK narrow, preset "auto"
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.0f, og.frequency);          // the derived band
    TEST_ASSERT_EQUAL_UINT8(c.radio.spreadingFactor, og.spreadingFactor);
    TEST_ASSERT_EQUAL_FLOAT(c.radio.bandwidth, og.bandwidth);
    TEST_ASSERT_EQUAL_UINT8(c.radio.codingRate, og.codingRate);
}

void test_resolve_auto_follows_the_users_band() {
    AppConfig c;
    c.radio.frequency = 910.525f;      // US
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(918.0f, og.frequency);
}

void test_resolve_meshcore_open_869() {
    AppConfig c;
    c.offgrid.preset = "mc_open";
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.0f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(c.radio.spreadingFactor, og.spreadingFactor);   // inherited
}

void test_resolve_meshcore_android_app_869_945() {
    AppConfig c;
    c.offgrid.preset = "mc_app";
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.945f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(c.radio.spreadingFactor, og.spreadingFactor);   // inherited
}

void test_resolve_fixed_band_ignores_user_frequency() {
    AppConfig c;
    c.radio.frequency = 869.618f;
    c.offgrid.preset = "mc_433";
    TEST_ASSERT_EQUAL_FLOAT(433.0f, resolveOffgrid(c.offgrid, c.radio).frequency);
    c.offgrid.preset = "mc_918";
    TEST_ASSERT_EQUAL_FLOAT(918.0f, resolveOffgrid(c.offgrid, c.radio).frequency);
}

void test_resolve_complete_preset_overrides_modem_settings() {
    // The point of a complete preset: two users whose normal regions differ still
    // end up on identical settings. Start from Netherlands (SF7/CR5), not EU narrow.
    AppConfig c;
    c.radio.frequency = 869.618f;
    c.radio.spreadingFactor = 7;
    c.radio.codingRate = 5;
    c.offgrid.preset = "mclite_869";
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.0f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(8, og.spreadingFactor);
    TEST_ASSERT_EQUAL_FLOAT(62.5f, og.bandwidth);
    TEST_ASSERT_EQUAL_UINT8(8, og.codingRate);
}

void test_resolve_custom_frequency_only_inherits_the_rest() {
    AppConfig c;
    c.offgrid.preset = "custom";
    c.offgrid.frequency = 869.4625f;
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.4625f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(c.radio.spreadingFactor, og.spreadingFactor);
    TEST_ASSERT_EQUAL_UINT8(c.radio.codingRate, og.codingRate);
}

void test_resolve_custom_full_override() {
    AppConfig c;
    c.offgrid.preset = "custom";
    c.offgrid.frequency = 869.4625f;
    c.offgrid.spreadingFactor = 11;
    c.offgrid.bandwidth = 125.0f;
    c.offgrid.codingRate = 5;
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.4625f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(11, og.spreadingFactor);
    TEST_ASSERT_EQUAL_FLOAT(125.0f, og.bandwidth);
    TEST_ASSERT_EQUAL_UINT8(5, og.codingRate);
}

void test_resolve_custom_with_nothing_set_falls_back_to_auto() {
    AppConfig c;
    c.offgrid.preset = "custom";        // but no fields filled in
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.0f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(c.radio.spreadingFactor, og.spreadingFactor);
}

void test_resolve_unknown_preset_falls_back_to_auto() {
    // A config written by a build whose preset list has since changed must still
    // boot, on the original behaviour, rather than picking an arbitrary entry.
    AppConfig c;
    c.offgrid.preset = "some_future_preset";
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.0f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(c.radio.spreadingFactor, og.spreadingFactor);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)offgridPresetIndex("some_future_preset"));
}

void test_resolve_empty_preset_falls_back_to_auto() {
    AppConfig c;
    c.offgrid.preset = "";
    TEST_ASSERT_EQUAL_FLOAT(869.0f, resolveOffgrid(c.offgrid, c.radio).frequency);
}

void test_preset_keys_are_unique() {
    for (size_t i = 0; i < OFFGRID_PRESET_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT(i, (unsigned)offgridPresetIndex(OFFGRID_PRESETS[i].key));
    }
}

// ═══ Config parse: the preset fields ═══

void test_offgrid_preset_defaults_to_auto_when_absent() {
    // The regression guard for every config.json written before presets existed.
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(
        String("{\"offgrid\":{\"enabled\":true}}")));
    TEST_ASSERT_EQUAL_STRING("auto", ConfigManager::instance().config().offgrid.preset.c_str());
}

void test_offgrid_preset_parsed() {
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(
        String("{\"offgrid\":{\"enabled\":true,\"preset\":\"mc_app\"}}")));
    TEST_ASSERT_EQUAL_STRING("mc_app", ConfigManager::instance().config().offgrid.preset.c_str());
}

void test_offgrid_custom_fields_round_trip() {
    ConfigManager::instance().config().offgrid.enabled = true;
    ConfigManager::instance().config().offgrid.preset = "custom";
    ConfigManager::instance().config().offgrid.frequency = 869.4625f;
    ConfigManager::instance().config().offgrid.spreadingFactor = 11;
    ConfigManager::instance().config().offgrid.bandwidth = 125.0f;
    ConfigManager::instance().config().offgrid.codingRate = 5;
    String json = ConfigManager::instance().toJson();
    ConfigManager::instance().config() = AppConfig{};
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(json));
    const auto& o = ConfigManager::instance().config().offgrid;
    TEST_ASSERT_EQUAL_STRING("custom", o.preset.c_str());
    TEST_ASSERT_EQUAL_FLOAT(869.4625f, o.frequency);
    TEST_ASSERT_EQUAL_UINT8(11, o.spreadingFactor);
    TEST_ASSERT_EQUAL_FLOAT(125.0f, o.bandwidth);
    TEST_ASSERT_EQUAL_UINT8(5, o.codingRate);
}

void test_offgrid_unset_custom_fields_are_not_serialized() {
    // A device on a named preset should not carry four meaningless zeroes.
    ConfigManager::instance().config().offgrid.enabled = true;
    ConfigManager::instance().config().offgrid.preset = "mc_open";
    String json = ConfigManager::instance().toJson();
    TEST_ASSERT_NULL(strstr(json.c_str(), "spreading_factor\":0"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "mc_open"));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_offgrid_433_lower_edge);
    RUN_TEST(test_offgrid_500_maps_to_433);
    RUN_TEST(test_offgrid_599_just_below_869_cut);
    RUN_TEST(test_offgrid_600_crosses_to_869);
    RUN_TEST(test_offgrid_eu_narrow_default);
    RUN_TEST(test_offgrid_eu_wide_default);
    RUN_TEST(test_offgrid_893_9_still_869);
    RUN_TEST(test_offgrid_894_crosses_to_918);
    RUN_TEST(test_offgrid_us_default);
    RUN_TEST(test_offgrid_918_itself);
    RUN_TEST(test_offgrid_960_upper);

    RUN_TEST(test_offgrid_missing_block_defaults_false);
    RUN_TEST(test_offgrid_enabled_true_parsed);
    RUN_TEST(test_offgrid_enabled_false_parsed);
    RUN_TEST(test_offgrid_serialized_round_trip);

    RUN_TEST(test_resolve_auto_is_the_old_behaviour);
    RUN_TEST(test_resolve_auto_follows_the_users_band);
    RUN_TEST(test_resolve_meshcore_open_869);
    RUN_TEST(test_resolve_meshcore_android_app_869_945);
    RUN_TEST(test_resolve_fixed_band_ignores_user_frequency);
    RUN_TEST(test_resolve_complete_preset_overrides_modem_settings);
    RUN_TEST(test_resolve_custom_frequency_only_inherits_the_rest);
    RUN_TEST(test_resolve_custom_full_override);
    RUN_TEST(test_resolve_custom_with_nothing_set_falls_back_to_auto);
    RUN_TEST(test_resolve_unknown_preset_falls_back_to_auto);
    RUN_TEST(test_resolve_empty_preset_falls_back_to_auto);
    RUN_TEST(test_preset_keys_are_unique);
    RUN_TEST(test_offgrid_preset_defaults_to_auto_when_absent);
    RUN_TEST(test_offgrid_preset_parsed);
    RUN_TEST(test_offgrid_custom_fields_round_trip);
    RUN_TEST(test_offgrid_unset_custom_fields_are_not_serialized);

    return UNITY_END();
}
