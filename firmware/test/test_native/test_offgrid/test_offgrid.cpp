#include <Arduino.h>
#include <unity.h>

#include "util/offgrid.h"

#define private public
#include "config/ConfigManager.h"
#undef private
#include "config/defaults.h"
#include "config/ConfigManager.cpp"

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

    return UNITY_END();
}
