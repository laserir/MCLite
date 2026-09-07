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

// ═══ Range checking: a bad number must never reach the radio ═══
//
// resolveOffgrid is the single gate, so every source (custom fields, a config
// preset, a future companion write) is covered by these.

void test_out_of_range_custom_values_are_ignored_not_clamped() {
    AppConfig c;
    c.offgrid.preset = "custom";
    c.offgrid.frequency = 42.0f;        // below the SX1262 floor
    c.offgrid.spreadingFactor = 99;
    c.offgrid.bandwidth = 9999.0f;
    c.offgrid.codingRate = 3;
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    // Inherited/derived, NOT clamped to an edge: 42 MHz would be a silent move
    // to a band the user never asked for.
    TEST_ASSERT_EQUAL_FLOAT(869.0f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(c.radio.spreadingFactor, og.spreadingFactor);
    TEST_ASSERT_EQUAL_FLOAT(c.radio.bandwidth, og.bandwidth);
    TEST_ASSERT_EQUAL_UINT8(c.radio.codingRate, og.codingRate);
}

void test_range_edges_are_accepted() {
    TEST_ASSERT_TRUE(offgridFreqValid(150.0f));   TEST_ASSERT_TRUE(offgridFreqValid(960.0f));
    TEST_ASSERT_FALSE(offgridFreqValid(149.9f));  TEST_ASSERT_FALSE(offgridFreqValid(960.1f));
    TEST_ASSERT_TRUE(offgridSfValid(5));          TEST_ASSERT_TRUE(offgridSfValid(12));
    TEST_ASSERT_FALSE(offgridSfValid(4));         TEST_ASSERT_FALSE(offgridSfValid(13));
    TEST_ASSERT_TRUE(offgridBwValid(7.8f));       TEST_ASSERT_TRUE(offgridBwValid(500.0f));
    TEST_ASSERT_FALSE(offgridBwValid(7.7f));      TEST_ASSERT_FALSE(offgridBwValid(500.1f));
    TEST_ASSERT_TRUE(offgridCrValid(5));          TEST_ASSERT_TRUE(offgridCrValid(8));
    TEST_ASSERT_FALSE(offgridCrValid(4));         TEST_ASSERT_FALSE(offgridCrValid(9));
}

void test_partially_valid_custom_keeps_the_good_fields() {
    AppConfig c;
    c.offgrid.preset = "custom";
    c.offgrid.frequency = 869.4625f;    // fine
    c.offgrid.spreadingFactor = 30;     // nonsense
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.4625f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(c.radio.spreadingFactor, og.spreadingFactor);
}

// ═══ User-defined presets (offgrid.presets[]) ═══

void test_user_preset_selected_by_name() {
    AppConfig c;
    OffgridUserPreset up;
    up.name = "club"; up.frequency = 869.4625f; up.spreadingFactor = 11;
    up.bandwidth = 125.0f; up.codingRate = 5;
    c.offgrid.presets.push_back(up);
    c.offgrid.preset = "club";
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.4625f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(11, og.spreadingFactor);
    TEST_ASSERT_EQUAL_FLOAT(125.0f, og.bandwidth);
    TEST_ASSERT_EQUAL_UINT8(5, og.codingRate);
}

void test_user_preset_partial_inherits_the_rest() {
    AppConfig c;
    OffgridUserPreset up; up.name = "just_freq"; up.frequency = 869.4625f;
    c.offgrid.presets.push_back(up);
    c.offgrid.preset = "just_freq";
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.4625f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(c.radio.spreadingFactor, og.spreadingFactor);
}

void test_user_preset_out_of_range_is_ignored() {
    AppConfig c;
    OffgridUserPreset up; up.name = "bad"; up.frequency = 5.0f; up.spreadingFactor = 99;
    c.offgrid.presets.push_back(up);
    c.offgrid.preset = "bad";
    OffgridRadio og = resolveOffgrid(c.offgrid, c.radio);
    TEST_ASSERT_EQUAL_FLOAT(869.0f, og.frequency);
    TEST_ASSERT_EQUAL_UINT8(c.radio.spreadingFactor, og.spreadingFactor);
}

void test_builtin_wins_over_a_user_preset_of_the_same_name() {
    // Otherwise a config could redefine "auto" and take the fallback path with it.
    AppConfig c;
    OffgridUserPreset up; up.name = "auto"; up.frequency = 700.0f;
    c.offgrid.presets.push_back(up);
    c.offgrid.preset = "auto";
    TEST_ASSERT_EQUAL_FLOAT(869.0f, resolveOffgrid(c.offgrid, c.radio).frequency);
}

void test_user_presets_parsed_and_round_tripped() {
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(String(
        "{\"offgrid\":{\"enabled\":true,\"preset\":\"club\",\"presets\":["
        "{\"name\":\"club\",\"frequency\":869.4625,\"spreading_factor\":11,"
        "\"bandwidth\":125,\"coding_rate\":5}]}}")));
    const auto& o = ConfigManager::instance().config().offgrid;
    TEST_ASSERT_EQUAL_UINT(1, (unsigned)o.presets.size());
    TEST_ASSERT_EQUAL_STRING("club", o.presets[0].name.c_str());
    TEST_ASSERT_EQUAL_FLOAT(869.4625f, o.presets[0].frequency);

    String json = ConfigManager::instance().toJson();
    ConfigManager::instance().config() = AppConfig{};
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(json));
    const auto& o2 = ConfigManager::instance().config().offgrid;
    TEST_ASSERT_EQUAL_UINT(1, (unsigned)o2.presets.size());
    TEST_ASSERT_EQUAL_UINT8(11, o2.presets[0].spreadingFactor);
    TEST_ASSERT_EQUAL_STRING("club", o2.preset.c_str());
}

void test_user_preset_without_a_name_is_skipped() {
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(String(
        "{\"offgrid\":{\"presets\":[{\"frequency\":869.4625}]}}")));
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)ConfigManager::instance().config().offgrid.presets.size());
}

void test_user_preset_shadowing_a_builtin_is_dropped_at_load() {
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(String(
        "{\"offgrid\":{\"presets\":[{\"name\":\"mc_app\",\"frequency\":700}]}}")));
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)ConfigManager::instance().config().offgrid.presets.size());
}

void test_user_presets_capped() {
    String j = "{\"offgrid\":{\"presets\":[";
    for (int i = 0; i < defaults::MAX_OFFGRID_PRESETS + 4; i++) {
        if (i) j += ",";
        j += "{\"name\":\"p" + String(i) + "\",\"frequency\":869.1}";
    }
    j += "]}}";
    TEST_ASSERT_TRUE(ConfigManager::instance().parseJson(j));
    TEST_ASSERT_EQUAL_UINT((unsigned)defaults::MAX_OFFGRID_PRESETS,
                           (unsigned)ConfigManager::instance().config().offgrid.presets.size());
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

    RUN_TEST(test_out_of_range_custom_values_are_ignored_not_clamped);
    RUN_TEST(test_range_edges_are_accepted);
    RUN_TEST(test_partially_valid_custom_keeps_the_good_fields);
    RUN_TEST(test_user_preset_selected_by_name);
    RUN_TEST(test_user_preset_partial_inherits_the_rest);
    RUN_TEST(test_user_preset_out_of_range_is_ignored);
    RUN_TEST(test_builtin_wins_over_a_user_preset_of_the_same_name);
    RUN_TEST(test_user_presets_parsed_and_round_tripped);
    RUN_TEST(test_user_preset_without_a_name_is_skipped);
    RUN_TEST(test_user_preset_shadowing_a_builtin_is_dropped_at_load);
    RUN_TEST(test_user_presets_capped);

    return UNITY_END();
}
