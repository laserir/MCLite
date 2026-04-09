#include <Arduino.h>
#include <unity.h>
#include <cstring>

// Standalone copy of computeShortId logic — avoids pulling in ContactStore deps.
// Keep in sync with mesh/ContactStore.cpp.
static String computeShortId(const uint8_t* publicKey) {
    char hex[17];
    for (int i = 0; i < 8; i++) {
        sprintf(hex + i * 2, "%02x", publicKey[i]);
    }
    hex[16] = '\0';
    return String(hex);
}

void test_shortid_basic() {
    uint8_t key[32];
    memset(key, 0, 32);
    key[0] = 0xAB; key[1] = 0xCD; key[2] = 0xEF; key[3] = 0x01;
    key[4] = 0x23; key[5] = 0x45; key[6] = 0x67; key[7] = 0x89;

    String id = computeShortId(key);
    TEST_ASSERT_EQUAL_STRING("abcdef0123456789", id.c_str());
}

void test_shortid_all_zeros() {
    uint8_t key[32];
    memset(key, 0, 32);

    String id = computeShortId(key);
    TEST_ASSERT_EQUAL_STRING("0000000000000000", id.c_str());
}

void test_shortid_all_ff() {
    uint8_t key[32];
    memset(key, 0xFF, 32);

    String id = computeShortId(key);
    TEST_ASSERT_EQUAL_STRING("ffffffffffffffff", id.c_str());
}

void test_shortid_length() {
    uint8_t key[32];
    memset(key, 0x42, 32);

    String id = computeShortId(key);
    TEST_ASSERT_EQUAL(16, id.length());
}

void test_shortid_ignores_bytes_after_8() {
    // Only first 8 bytes matter
    uint8_t key1[32], key2[32];
    memset(key1, 0xAA, 32);
    memset(key2, 0xAA, 8);
    memset(key2 + 8, 0xBB, 24);

    String id1 = computeShortId(key1);
    String id2 = computeShortId(key2);
    TEST_ASSERT_EQUAL_STRING(id1.c_str(), id2.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_shortid_basic);
    RUN_TEST(test_shortid_all_zeros);
    RUN_TEST(test_shortid_all_ff);
    RUN_TEST(test_shortid_length);
    RUN_TEST(test_shortid_ignores_bytes_after_8);
    return UNITY_END();
}
