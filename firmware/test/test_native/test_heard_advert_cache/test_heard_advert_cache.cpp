#include <Arduino.h>  // stub: provides millis(), stub_set_millis()
#include <unity.h>

#define private public
#include "storage/HeardAdvertCache.h"
#undef private

using namespace mclite;

static void makeKey(uint8_t* key, uint8_t val) {
    memset(key, val, 32);
}

static HeardAdvertCache cache;

void setUp() {
    cache.~HeardAdvertCache();
    new (&cache) HeardAdvertCache();
    stub_set_millis(100000);
}

void tearDown() {}

void test_store_new_entry() {
    uint8_t key[32];
    makeKey(key, 0x01);

    cache.store(key, "alice", 1, 2, nullptr, 489000000, 23000000);

    TEST_ASSERT_EQUAL_INT(1, cache.count());
    const HeardAdvert* es = cache.entries();
    TEST_ASSERT_EQUAL_STRING("alice", es[0].name);
    TEST_ASSERT_EQUAL_UINT8(1, es[0].type);
    TEST_ASSERT_EQUAL_UINT8(2, es[0].hops);
    TEST_ASSERT_TRUE(es[0].hasGps);
    TEST_ASSERT_EQUAL_INT32(489000000, es[0].gpsLat);
}

void test_path_len_mask_strips_hash_size() {
    // Packed path_len = (hash_size_mode << 6) | hop_count
    // Raw 70 = 0b01000110 → mode 1 (= 2 bytes per hash), 6 hops
    // Raw 0xC1 = mode 3 (= 4 bytes per hash), 1 hop
    uint8_t key1[32]; makeKey(key1, 0xA1);
    cache.store(key1, "x", 1, 70, nullptr, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(6, cache.entries()[0].hops);
    TEST_ASSERT_EQUAL_UINT8(2, cache.entries()[0].hashSize);

    uint8_t key2[32]; makeKey(key2, 0xA2);
    cache.store(key2, "y", 1, 0xC1, nullptr, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(1, cache.entries()[1].hops);
    TEST_ASSERT_EQUAL_UINT8(4, cache.entries()[1].hashSize);

    // Maximum hop count is 63 even when raw byte is 0xFF
    uint8_t key3[32]; makeKey(key3, 0xA3);
    cache.store(key3, "z", 1, 0xFF, nullptr, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(63, cache.entries()[2].hops);
    TEST_ASSERT_EQUAL_UINT8(4, cache.entries()[2].hashSize);
}

void test_version_bumps_on_each_store() {
    uint32_t v0 = cache.version();

    uint8_t k1[32]; makeKey(k1, 0xB1);
    cache.store(k1, "a", 1, 0, nullptr, 0, 0);
    TEST_ASSERT_TRUE(cache.version() > v0);
    uint32_t v1 = cache.version();

    cache.store(k1, "a", 1, 1, nullptr, 0, 0);
    TEST_ASSERT_TRUE(cache.version() > v1);
}

void test_path_bytes_stored() {
    uint8_t key[32]; makeKey(key, 0xC0);
    // raw path_len = (1 << 6) | 3 = 67 → mode 1 (2-byte hash), 3 hops, 6 path bytes
    uint8_t path[6] = { 0xab, 0xcd, 0x12, 0x34, 0xef, 0x01 };
    cache.store(key, "router", 2, 67, path, 0, 0);

    const HeardAdvert& e = cache.entries()[0];
    TEST_ASSERT_EQUAL_UINT8(3, e.hops);
    TEST_ASSERT_EQUAL_UINT8(2, e.hashSize);
    TEST_ASSERT_EQUAL_UINT8(6, e.pathByteLen);
    TEST_ASSERT_EQUAL_HEX8(0xab, e.pathBytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xcd, e.pathBytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, e.pathBytes[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, e.pathBytes[6]);  // tail zeroed
}

void test_path_truncated_to_cap() {
    // Pathological: 63 hops × 4 bytes = 252 bytes > HEARD_PATH_CAP (64)
    // Should clamp at the cap without overrunning the buffer.
    uint8_t key[32]; makeKey(key, 0xC1);
    uint8_t big[252];
    for (int i = 0; i < 252; i++) big[i] = (uint8_t)i;
    cache.store(key, "x", 1, 0xFF, big, 0, 0);

    const HeardAdvert& e = cache.entries()[0];
    TEST_ASSERT_EQUAL_UINT8(63, e.hops);
    TEST_ASSERT_EQUAL_UINT8(4, e.hashSize);
    TEST_ASSERT_EQUAL_UINT8(HEARD_PATH_CAP, e.pathByteLen);
    TEST_ASSERT_EQUAL_HEX8(0x00, e.pathBytes[0]);
    TEST_ASSERT_EQUAL_HEX8(63,    e.pathBytes[63]);
}

void test_path_replaced_on_update() {
    uint8_t key[32]; makeKey(key, 0xC2);
    uint8_t p1[2] = { 0xaa, 0xbb };
    cache.store(key, "n", 1, 65, p1, 0, 0);  // 1 hop, 2-byte hash
    TEST_ASSERT_EQUAL_UINT8(2, cache.entries()[0].pathByteLen);

    // Re-heard with a shorter path
    uint8_t p2[1] = { 0xcc };
    cache.store(key, "n", 1, 1, p2, 0, 0);   // 1 hop, 1-byte hash
    TEST_ASSERT_EQUAL_INT(1, cache.count());
    TEST_ASSERT_EQUAL_UINT8(1, cache.entries()[0].pathByteLen);
    TEST_ASSERT_EQUAL_UINT8(1, cache.entries()[0].hashSize);
    TEST_ASSERT_EQUAL_HEX8(0xcc, cache.entries()[0].pathBytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, cache.entries()[0].pathBytes[1]);  // old byte cleared
}

void test_empty_name_stored() {
    uint8_t key[32];
    makeKey(key, 0xAA);

    cache.store(key, nullptr, 4, 0, nullptr, 0, 0);

    TEST_ASSERT_EQUAL_INT(1, cache.count());
    const HeardAdvert* es = cache.entries();
    TEST_ASSERT_EQUAL_STRING("", es[0].name);
    TEST_ASSERT_FALSE(es[0].hasGps);
    TEST_ASSERT_EQUAL_UINT8(0, es[0].pathByteLen);
}

void test_update_in_place() {
    uint8_t key[32];
    makeKey(key, 0x05);

    cache.store(key, "bob", 1, 3, nullptr, 0, 0);
    stub_set_millis(200000);
    cache.store(key, "bob_renamed", 1, 1, nullptr, 100, 200);

    TEST_ASSERT_EQUAL_INT(1, cache.count());
    const HeardAdvert* es = cache.entries();
    TEST_ASSERT_EQUAL_STRING("bob_renamed", es[0].name);
    TEST_ASSERT_EQUAL_UINT8(1, es[0].hops);
    TEST_ASSERT_EQUAL_UINT32(200000, es[0].lastHeardMs);
}

void test_eviction_at_capacity() {
    for (int i = 0; i < HEARD_ADVERT_CAP; i++) {
        uint8_t k[32];
        makeKey(k, (uint8_t)i);
        stub_set_millis(100000 + i);
        cache.store(k, "x", 1, 0, nullptr, 0, 0);
    }
    TEST_ASSERT_EQUAL_INT(HEARD_ADVERT_CAP, cache.count());

    uint8_t newKey[32];
    makeKey(newKey, 0xFE);
    stub_set_millis(200000);
    cache.store(newKey, "newcomer", 1, 0, nullptr, 0, 0);

    TEST_ASSERT_EQUAL_INT(HEARD_ADVERT_CAP, cache.count());

    uint8_t oldKey[32];
    makeKey(oldKey, 0x00);
    bool foundOld = false;
    for (int i = 0; i < cache.count(); i++) {
        if (memcmp(cache.entries()[i].pubKey, oldKey, 32) == 0) {
            foundOld = true;
            break;
        }
    }
    TEST_ASSERT_FALSE(foundOld);

    bool foundNew = false;
    for (int i = 0; i < cache.count(); i++) {
        if (memcmp(cache.entries()[i].pubKey, newKey, 32) == 0) {
            foundNew = true;
            TEST_ASSERT_EQUAL_STRING("newcomer", cache.entries()[i].name);
            break;
        }
    }
    TEST_ASSERT_TRUE(foundNew);
}

void test_no_growth_on_repeat_insert() {
    uint8_t key[32];
    makeKey(key, 0x42);
    for (int i = 0; i < 100; i++) {
        cache.store(key, "spam", 1, 0, nullptr, 0, 0);
    }
    TEST_ASSERT_EQUAL_INT(1, cache.count());
}

void test_distinct_keys_grow() {
    for (int i = 0; i < 5; i++) {
        uint8_t k[32];
        makeKey(k, (uint8_t)(0x10 + i));
        cache.store(k, "n", 1, 0, nullptr, 0, 0);
    }
    TEST_ASSERT_EQUAL_INT(5, cache.count());
}

void test_gps_zero_means_no_gps() {
    uint8_t key[32];
    makeKey(key, 0x77);
    cache.store(key, "ghost", 1, 0, nullptr, 0, 0);
    TEST_ASSERT_FALSE(cache.entries()[0].hasGps);
}

void test_gps_nonzero_lat_only() {
    uint8_t key[32];
    makeKey(key, 0x78);
    cache.store(key, "n", 1, 0, nullptr, 0, 1);
    TEST_ASSERT_TRUE(cache.entries()[0].hasGps);
}

void test_clear_empties_buffer_and_bumps_version() {
    uint8_t k1[32]; makeKey(k1, 0x80);
    uint8_t k2[32]; makeKey(k2, 0x81);
    cache.store(k1, "a", 1, 0, nullptr, 0, 0);
    cache.store(k2, "b", 1, 0, nullptr, 0, 0);
    TEST_ASSERT_EQUAL_INT(2, cache.count());

    uint32_t v = cache.version();
    cache.clear();
    TEST_ASSERT_EQUAL_INT(0, cache.count());
    TEST_ASSERT_TRUE(cache.version() > v);  // live UIs poll version
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_store_new_entry);
    RUN_TEST(test_path_len_mask_strips_hash_size);
    RUN_TEST(test_version_bumps_on_each_store);
    RUN_TEST(test_path_bytes_stored);
    RUN_TEST(test_path_truncated_to_cap);
    RUN_TEST(test_path_replaced_on_update);
    RUN_TEST(test_empty_name_stored);
    RUN_TEST(test_update_in_place);
    RUN_TEST(test_eviction_at_capacity);
    RUN_TEST(test_no_growth_on_repeat_insert);
    RUN_TEST(test_distinct_keys_grow);
    RUN_TEST(test_gps_zero_means_no_gps);
    RUN_TEST(test_gps_nonzero_lat_only);
    RUN_TEST(test_clear_empties_buffer_and_bumps_version);
    return UNITY_END();
}
