#include <Arduino.h>  // stub: provides millis(), stub_set_millis()
#include <unity.h>

// Make TelemetryCache constructor accessible for testing
#define private public
#include "storage/TelemetryCache.h"
#undef private

using namespace mclite;

static void makeKey(uint8_t* key, uint8_t val) {
    memset(key, val, 32);
}

// Single test instance — manually reset between tests
static TelemetryCache cache;

void setUp() {
    // Reset cache state: zero out and reconstruct
    cache.~TelemetryCache();
    new (&cache) TelemetryCache();
    stub_set_millis(100000);
}

void tearDown() {}

void test_store_and_get() {
    uint8_t key[32];
    makeKey(key, 0x01);

    TelemetryData data;
    data.voltage = 3.7f;
    data.hasVoltage = true;
    data.receivedAt = millis();

    cache.store(key, data);
    const TelemetryData* got = cache.get(key);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_FLOAT(3.7f, got->voltage);
    TEST_ASSERT_TRUE(got->hasVoltage);
}

void test_get_missing_returns_null() {
    uint8_t key[32];
    makeKey(key, 0xAA);
    TEST_ASSERT_NULL(cache.get(key));
}

void test_overwrite_existing() {
    uint8_t key[32];
    makeKey(key, 0x02);

    TelemetryData d1;
    d1.voltage = 3.5f;
    d1.hasVoltage = true;
    d1.receivedAt = millis();
    cache.store(key, d1);

    TelemetryData d2;
    d2.voltage = 4.1f;
    d2.hasVoltage = true;
    d2.receivedAt = millis();
    cache.store(key, d2);

    const TelemetryData* got = cache.get(key);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_FLOAT(4.1f, got->voltage);
}

void test_fresh_entry_is_fresh() {
    uint8_t key[32];
    makeKey(key, 0x03);

    TelemetryData data;
    data.receivedAt = millis();
    cache.store(key, data);

    TEST_ASSERT_TRUE(cache.isFresh(key));
}

void test_stale_entry() {
    uint8_t key[32];
    makeKey(key, 0x04);

    TelemetryData data;
    data.receivedAt = millis();
    cache.store(key, data);

    stub_set_millis(millis() + TelemetryCache::STALE_MS + 1);
    TEST_ASSERT_FALSE(cache.isFresh(key));
}

void test_missing_key_not_fresh() {
    uint8_t key[32];
    makeKey(key, 0xFF);
    TEST_ASSERT_FALSE(cache.isFresh(key));
}

void test_eviction_when_full() {
    for (int i = 0; i < MAX_CONTACTS; i++) {
        uint8_t key[32];
        makeKey(key, (uint8_t)i);
        TelemetryData data;
        data.voltage = (float)i;
        data.hasVoltage = true;
        data.receivedAt = millis() + i;
        cache.store(key, data);
    }

    uint8_t newKey[32];
    makeKey(newKey, 0xFF);
    TelemetryData newData;
    newData.voltage = 99.0f;
    newData.hasVoltage = true;
    newData.receivedAt = millis() + MAX_CONTACTS;
    cache.store(newKey, newData);

    const TelemetryData* got = cache.get(newKey);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_FLOAT(99.0f, got->voltage);

    uint8_t oldKey[32];
    makeKey(oldKey, 0x00);
    TEST_ASSERT_NULL(cache.get(oldKey));
}

void test_location_data() {
    uint8_t key[32];
    makeKey(key, 0x10);

    TelemetryData data;
    data.lat = 48.8566;
    data.lon = 2.3522;
    data.alt = 35.0;
    data.hasLocation = true;
    data.receivedAt = millis();
    cache.store(key, data);

    const TelemetryData* got = cache.get(key);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_TRUE(got->hasLocation);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 48.8566f, (float)got->lat);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_store_and_get);
    RUN_TEST(test_get_missing_returns_null);
    RUN_TEST(test_overwrite_existing);
    RUN_TEST(test_fresh_entry_is_fresh);
    RUN_TEST(test_stale_entry);
    RUN_TEST(test_missing_key_not_fresh);
    RUN_TEST(test_eviction_when_full);
    RUN_TEST(test_location_data);
    return UNITY_END();
}
