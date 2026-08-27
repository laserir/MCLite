// Tests for the MeshCore One reactions message hash (util/MsgHash.h).
//
// The hash is an interop contract with a third-party app, so these lock in the
// exact construction: SHA-256( UTF-8(text) || LE32(senderTimestamp) ), first 5
// bytes, MSB-first Crockford Base32, lowercase. Expected values were derived
// independently with Python's hashlib, not from this implementation.
#include <unity.h>
#include <cstring>
#include "util/MsgHash.h"

using namespace mclite;

// ═══ The stub's SHA-256 must be real, or every test below is vacuous ═══

void test_sha256_stub_matches_known_vector() {
    uint8_t out[32];
    mbedtls_sha256((const uint8_t*)"abc", 3, out, 0);
    // FIPS 180-4: SHA-256("abc") = ba7816bf 8f01cfea ...
    TEST_ASSERT_EQUAL_HEX8(0xba, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x78, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x16, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xbf, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x8f, out[4]);
}

// ═══ computeMsgHash — spec construction ═══

void test_hash_is_8_chars() {
    TEST_ASSERT_EQUAL(8, computeMsgHash("Hello", 1700000000).length());
}

void test_hash_known_value() {
    TEST_ASSERT_EQUAL_STRING("ba6ma8jy", computeMsgHash("Hello", 1700000000).c_str());
}

// A one-second timestamp change must change the hash — catches the timestamp
// being dropped from the hashed input entirely.
void test_hash_depends_on_timestamp() {
    TEST_ASSERT_EQUAL_STRING("w7jv69ta", computeMsgHash("Hello", 1700000001).c_str());
}

// Guards the little-endian packing: 0x01020304 byte-swapped would hash differently.
void test_hash_timestamp_is_little_endian() {
    TEST_ASSERT_EQUAL_STRING("aghr1v6e", computeMsgHash("hi", 0x01020304).c_str());
}

void test_hash_empty_text() {
    TEST_ASSERT_EQUAL_STRING("cynxtwgg", computeMsgHash("", 1).c_str());
}

// The spec's examples are lowercase; a peer comparing case-sensitively must match.
void test_hash_is_lowercase() {
    String h = computeMsgHash("Hello", 1700000000);
    for (size_t i = 0; i < h.length(); i++) {
        char c = h[i];
        TEST_ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'));
    }
}

// Crockford excludes I, L, O and U.
void test_hash_uses_crockford_alphabet() {
    for (uint32_t ts = 1700000000; ts < 1700000200; ts++) {
        String h = computeMsgHash("x", ts);
        for (size_t i = 0; i < h.length(); i++) {
            char c = h[i];
            TEST_ASSERT_TRUE(c != 'i' && c != 'l' && c != 'o' && c != 'u');
        }
    }
}

// ═══ isCrockfordB32 ═══

void test_valid_crockford_accepted() {
    TEST_ASSERT_TRUE(isCrockfordB32("ba6ma8jy"));
    TEST_ASSERT_TRUE(isCrockfordB32("BA6MA8JY"));      // case-insensitive
    TEST_ASSERT_TRUE(isCrockfordB32("0oi1lLIO"));      // O/I/L are aliases, not invalid
}

void test_wrong_length_rejected() {
    TEST_ASSERT_FALSE(isCrockfordB32("ba6ma8j"));      // 7
    TEST_ASSERT_FALSE(isCrockfordB32("ba6ma8jyz"));    // 9
    TEST_ASSERT_FALSE(isCrockfordB32(""));
}

void test_excluded_letter_rejected() {
    TEST_ASSERT_FALSE(isCrockfordB32("uuuuuuuu"));     // U is excluded
    TEST_ASSERT_FALSE(isCrockfordB32("ba6ma8j-"));
    TEST_ASSERT_FALSE(isCrockfordB32("ba6ma8j "));
}

// ═══ normalizeCrockford — MUST agree with computeMsgHash's alphabet ═══
// applyReaction() compares a normalized inbound hash against the stored one with
// ==, so a case mismatch between these two silently breaks every reaction.

void test_normalize_is_lowercase_and_matches_compute() {
    String computed = computeMsgHash("Hello", 1700000000);
    TEST_ASSERT_EQUAL_STRING(computed.c_str(), normalizeCrockford(computed).c_str());
    // An inbound uppercase hash from a peer must normalize onto our stored form.
    String upper = computed; upper.toUpperCase();
    TEST_ASSERT_EQUAL_STRING(computed.c_str(), normalizeCrockford(upper).c_str());
}

void test_normalize_maps_oil_aliases() {
    // O/o -> 0, I/i/L/l -> 1, digits unchanged.
    TEST_ASSERT_EQUAL_STRING("00111100", normalizeCrockford("OoIiLl00").c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sha256_stub_matches_known_vector);
    RUN_TEST(test_hash_is_8_chars);
    RUN_TEST(test_hash_known_value);
    RUN_TEST(test_hash_depends_on_timestamp);
    RUN_TEST(test_hash_timestamp_is_little_endian);
    RUN_TEST(test_hash_empty_text);
    RUN_TEST(test_hash_is_lowercase);
    RUN_TEST(test_hash_uses_crockford_alphabet);
    RUN_TEST(test_valid_crockford_accepted);
    RUN_TEST(test_wrong_length_rejected);
    RUN_TEST(test_excluded_letter_rejected);
    RUN_TEST(test_normalize_is_lowercase_and_matches_compute);
    RUN_TEST(test_normalize_maps_oil_aliases);
    return UNITY_END();
}
