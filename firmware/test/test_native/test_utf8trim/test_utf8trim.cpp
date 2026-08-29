// Tests for truncateUtf8 (util/Utf8Trim.h).
//
// MeshCore truncates over-long text itself, at a raw byte offset, which can cut a
// multi-byte character in half and leave the receiver storing invalid UTF-8. The
// SOS and battery-alert paths clamp with this first, so the boundary behaviour is
// worth pinning: those are the messages that matter most on this device.
#include <unity.h>
#include "util/Utf8Trim.h"

using namespace mclite;

static const char* EURO = "\xE2\x82\xAC";          // U+20AC, 3 bytes
static const char* GRIN = "\xF0\x9F\x98\x80";      // U+1F600, 4 bytes

void test_short_string_unchanged() {
    TEST_ASSERT_EQUAL_STRING("hello", truncateUtf8("hello", 160).c_str());
}

void test_exact_fit_unchanged() {
    TEST_ASSERT_EQUAL_STRING("hello", truncateUtf8("hello", 5).c_str());
}

void test_ascii_truncates_at_limit() {
    TEST_ASSERT_EQUAL_STRING("hel", truncateUtf8("hello", 3).c_str());
}

// The whole point: never leave a partial sequence behind.
void test_never_splits_a_3_byte_sequence() {
    String s = String("ab") + EURO;             // 5 bytes
    TEST_ASSERT_EQUAL_STRING("ab", truncateUtf8(s, 3).c_str());   // mid-euro
    TEST_ASSERT_EQUAL_STRING("ab", truncateUtf8(s, 4).c_str());   // mid-euro
    TEST_ASSERT_EQUAL_STRING(s.c_str(), truncateUtf8(s, 5).c_str());
}

void test_never_splits_a_4_byte_sequence() {
    String s = String("a") + GRIN;              // 5 bytes
    for (size_t n = 1; n <= 4; n++) {
        TEST_ASSERT_EQUAL_STRING("a", truncateUtf8(s, n).c_str());
    }
    TEST_ASSERT_EQUAL_STRING(s.c_str(), truncateUtf8(s, 5).c_str());
}

// A cut landing exactly on a lead byte keeps everything before it.
void test_cut_on_lead_byte_keeps_prefix() {
    String s = String(EURO) + EURO;             // 6 bytes
    TEST_ASSERT_EQUAL_STRING(EURO, truncateUtf8(s, 3).c_str());
}

void test_zero_budget_yields_empty() {
    TEST_ASSERT_EQUAL_STRING("", truncateUtf8("hello", 0).c_str());
    TEST_ASSERT_EQUAL_STRING("", truncateUtf8(String(GRIN), 0).c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_short_string_unchanged);
    RUN_TEST(test_exact_fit_unchanged);
    RUN_TEST(test_ascii_truncates_at_limit);
    RUN_TEST(test_never_splits_a_3_byte_sequence);
    RUN_TEST(test_never_splits_a_4_byte_sequence);
    RUN_TEST(test_cut_on_lead_byte_keeps_prefix);
    RUN_TEST(test_zero_budget_yields_empty);
    return UNITY_END();
}
