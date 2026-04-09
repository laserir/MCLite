#include <unity.h>
#include <Arduino.h>

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
    return UNITY_END();
}
