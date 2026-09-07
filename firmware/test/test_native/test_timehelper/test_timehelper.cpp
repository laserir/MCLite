#include <unity.h>
#include <Arduino.h>

// formatClock lives in the header precisely so it can be tested for real rather
// than copied. Do not let TimeHelper.h grow heavy includes or this breaks.
#include "util/TimeHelper.h"
using mclite::TimeHelper;

// Standalone copy of isValidPosixTz to avoid pulling in ConfigManager deps.
// Keep in sync with src/util/TimeHelper.cpp.
static bool isValidPosixTz(const String& tz) {
    bool hasAlpha = false, hasDigit = false;
    for (size_t i = 0; i < tz.length(); i++) {
        char c = tz[i];
        if (isalpha(c)) hasAlpha = true;
        if (isdigit(c)) { hasDigit = true; break; }
    }
    return hasAlpha && hasDigit;
}

void test_posix_tz_valid_cet()      { TEST_ASSERT_TRUE(isValidPosixTz("CET-1CEST,M3.5.0,M10.5.0/3")); }
void test_posix_tz_valid_utc0()     { TEST_ASSERT_TRUE(isValidPosixTz("UTC0")); }
void test_posix_tz_valid_est()      { TEST_ASSERT_TRUE(isValidPosixTz("EST5EDT")); }
void test_posix_tz_valid_aest()     { TEST_ASSERT_TRUE(isValidPosixTz("AEST-10AEDT,M10.1.0,M4.1.0/3")); }
void test_posix_tz_empty()          { TEST_ASSERT_FALSE(isValidPosixTz("")); }
void test_posix_tz_digits_only()    { TEST_ASSERT_FALSE(isValidPosixTz("123")); }
void test_posix_tz_alpha_only()     { TEST_ASSERT_FALSE(isValidPosixTz("CET")); }
void test_posix_tz_garbage()        { TEST_ASSERT_FALSE(isValidPosixTz("!!!")); }
void test_posix_tz_negative_offset(){ TEST_ASSERT_TRUE(isValidPosixTz("IST-5")); }

// ═══ formatClock: 24-hour is unchanged ═══

void test_clock_24h_midnight() {
    char b[TimeHelper::CLOCK_BUF];
    TEST_ASSERT_TRUE(TimeHelper::formatClock(b, sizeof(b), 0, 0, false));
    TEST_ASSERT_EQUAL_STRING("00:00", b);
}
void test_clock_24h_evening() {
    char b[TimeHelper::CLOCK_BUF];
    TimeHelper::formatClock(b, sizeof(b), 23, 45, false);
    TEST_ASSERT_EQUAL_STRING("23:45", b);
}
void test_clock_24h_pads_hour() {
    char b[TimeHelper::CLOCK_BUF];
    TimeHelper::formatClock(b, sizeof(b), 9, 5, false);
    TEST_ASSERT_EQUAL_STRING("09:05", b);
}

// ═══ formatClock: the 12-hour edges that are easy to get wrong ═══

void test_clock_12h_midnight_is_12am() {
    // 0 % 12 == 0, so without the fixup this would render "0:00 AM".
    char b[TimeHelper::CLOCK_BUF];
    TimeHelper::formatClock(b, sizeof(b), 0, 0, true);
    TEST_ASSERT_EQUAL_STRING("12:00 AM", b);
}
void test_clock_12h_noon_is_12pm() {
    char b[TimeHelper::CLOCK_BUF];
    TimeHelper::formatClock(b, sizeof(b), 12, 0, true);
    TEST_ASSERT_EQUAL_STRING("12:00 PM", b);
}
void test_clock_12h_just_before_noon_is_am() {
    char b[TimeHelper::CLOCK_BUF];
    TimeHelper::formatClock(b, sizeof(b), 11, 59, true);
    TEST_ASSERT_EQUAL_STRING("11:59 AM", b);
}
void test_clock_12h_just_after_noon_is_pm() {
    char b[TimeHelper::CLOCK_BUF];
    TimeHelper::formatClock(b, sizeof(b), 13, 0, true);
    TEST_ASSERT_EQUAL_STRING("1:00 PM", b);
}
void test_clock_12h_late_evening() {
    char b[TimeHelper::CLOCK_BUF];
    TimeHelper::formatClock(b, sizeof(b), 23, 45, true);
    TEST_ASSERT_EQUAL_STRING("11:45 PM", b);
}
void test_clock_12h_early_morning() {
    char b[TimeHelper::CLOCK_BUF];
    TimeHelper::formatClock(b, sizeof(b), 1, 5, true);
    TEST_ASSERT_EQUAL_STRING("1:05 AM", b);
}
void test_clock_12h_hour_is_not_zero_padded() {
    // Pins the width decision: "9:05 AM" is 7 chars, "09:05 AM" would be 8.
    char b[TimeHelper::CLOCK_BUF];
    TimeHelper::formatClock(b, sizeof(b), 9, 5, true);
    TEST_ASSERT_EQUAL_STRING("9:05 AM", b);
}

// ═══ Guards: a short buffer must blank, never truncate ═══

void test_clock_rejects_short_buffer_12h() {
    // char[8] is what every call site used before this feature; in 12-hour mode
    // it must refuse rather than render a clipped time.
    char b[8] = "NOTSET";   // pre-filled: the guard must blank it, not leave it
    TEST_ASSERT_FALSE(TimeHelper::formatClock(b, 8, 12, 45, true));
    TEST_ASSERT_EQUAL_STRING("", b);
}
void test_clock_exact_fit_12h_succeeds() {
    // 9 bytes is the exact minimum: "12:45 AM" is 8 chars + NUL, and midnight is
    // the widest case since hour 12 and hour 0 both render as "12".
    char b[9];
    TEST_ASSERT_TRUE(TimeHelper::formatClock(b, sizeof(b), 0, 45, true));
    TEST_ASSERT_EQUAL_STRING("12:45 AM", b);
}
void test_clock_rejects_short_buffer_24h() {
    char b[8];
    TEST_ASSERT_FALSE(TimeHelper::formatClock(b, 5, 23, 45, false));
    TEST_ASSERT_EQUAL_STRING("", b);
}
void test_clock_rejects_out_of_range() {
    char b[TimeHelper::CLOCK_BUF];
    TEST_ASSERT_FALSE(TimeHelper::formatClock(b, sizeof(b), 24, 0, true));
    TEST_ASSERT_EQUAL_STRING("", b);
    TEST_ASSERT_FALSE(TimeHelper::formatClock(b, sizeof(b), 12, 60, true));
    TEST_ASSERT_EQUAL_STRING("", b);
    TEST_ASSERT_FALSE(TimeHelper::formatClock(b, sizeof(b), -1, 0, false));
    TEST_ASSERT_EQUAL_STRING("", b);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_posix_tz_valid_cet);
    RUN_TEST(test_posix_tz_valid_utc0);
    RUN_TEST(test_posix_tz_valid_est);
    RUN_TEST(test_posix_tz_valid_aest);
    RUN_TEST(test_posix_tz_empty);
    RUN_TEST(test_posix_tz_digits_only);
    RUN_TEST(test_posix_tz_alpha_only);
    RUN_TEST(test_posix_tz_garbage);
    RUN_TEST(test_posix_tz_negative_offset);

    RUN_TEST(test_clock_24h_midnight);
    RUN_TEST(test_clock_24h_evening);
    RUN_TEST(test_clock_24h_pads_hour);
    RUN_TEST(test_clock_12h_midnight_is_12am);
    RUN_TEST(test_clock_12h_noon_is_12pm);
    RUN_TEST(test_clock_12h_just_before_noon_is_am);
    RUN_TEST(test_clock_12h_just_after_noon_is_pm);
    RUN_TEST(test_clock_12h_late_evening);
    RUN_TEST(test_clock_12h_early_morning);
    RUN_TEST(test_clock_12h_hour_is_not_zero_padded);
    RUN_TEST(test_clock_rejects_short_buffer_12h);
    RUN_TEST(test_clock_exact_fit_12h_succeeds);
    RUN_TEST(test_clock_rejects_short_buffer_24h);
    RUN_TEST(test_clock_rejects_out_of_range);
    return UNITY_END();
}
