#include <unity.h>
#include "util/epoch.h"

using namespace mclite;

// Reference values verified against https://www.epochconverter.com/

void test_epoch_unix_epoch_start() {
    // 1970-01-01 is before 2024 cutoff → returns 0
    TEST_ASSERT_EQUAL_UINT32(0, dateToEpoch(1970, 1, 1, 0, 0, 0));
}

void test_epoch_2024_jan_1() {
    // 2024-01-01 00:00:00 UTC = 1704067200
    TEST_ASSERT_EQUAL_UINT32(1704067200UL, dateToEpoch(2024, 1, 1, 0, 0, 0));
}

void test_epoch_2024_jul_4_noon() {
    // 2024-07-04 12:00:00 UTC = 1720094400
    TEST_ASSERT_EQUAL_UINT32(1720094400UL, dateToEpoch(2024, 7, 4, 12, 0, 0));
}

void test_epoch_2025_mar_15() {
    // 2025-03-15 08:30:45 UTC = 1742027445
    TEST_ASSERT_EQUAL_UINT32(1742027445UL, dateToEpoch(2025, 3, 15, 8, 30, 45));
}

void test_epoch_leap_year_feb_29_2024() {
    // 2024 is a leap year. 2024-02-29 00:00:00 UTC = 1709164800
    TEST_ASSERT_EQUAL_UINT32(1709164800UL, dateToEpoch(2024, 2, 29, 0, 0, 0));
}

void test_epoch_non_leap_year_2025() {
    // 2025-03-01 00:00:00 UTC = 1740787200
    TEST_ASSERT_EQUAL_UINT32(1740787200UL, dateToEpoch(2025, 3, 1, 0, 0, 0));
}

void test_epoch_end_of_day() {
    // 2024-01-01 23:59:59 UTC = 1704067200 + 86399 = 1704153599
    TEST_ASSERT_EQUAL_UINT32(1704153599UL, dateToEpoch(2024, 1, 1, 23, 59, 59));
}

void test_epoch_december_31() {
    // 2024-12-31 23:59:59 UTC = 1735689599
    TEST_ASSERT_EQUAL_UINT32(1735689599UL, dateToEpoch(2024, 12, 31, 23, 59, 59));
}

void test_epoch_invalid_year_zero() {
    TEST_ASSERT_EQUAL_UINT32(0, dateToEpoch(0, 1, 1, 0, 0, 0));
}

void test_epoch_invalid_year_2023() {
    TEST_ASSERT_EQUAL_UINT32(0, dateToEpoch(2023, 12, 31, 23, 59, 59));
}

void test_epoch_invalid_month_zero() {
    TEST_ASSERT_EQUAL_UINT32(0, dateToEpoch(2024, 0, 1, 0, 0, 0));
}

void test_epoch_invalid_day_zero() {
    TEST_ASSERT_EQUAL_UINT32(0, dateToEpoch(2024, 1, 0, 0, 0, 0));
}

void test_epoch_century_not_leap() {
    // 2100 is NOT a leap year (divisible by 100 but not 400)
    // 2100-03-01 00:00:00 — tests that Feb has 28 days in 2100
    // This is a far-future date but validates the leap year logic
    uint32_t mar1 = dateToEpoch(2100, 3, 1, 0, 0, 0);
    uint32_t feb28 = dateToEpoch(2100, 2, 28, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(86400, mar1 - feb28);  // exactly 1 day apart
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_epoch_unix_epoch_start);
    RUN_TEST(test_epoch_2024_jan_1);
    RUN_TEST(test_epoch_2024_jul_4_noon);
    RUN_TEST(test_epoch_2025_mar_15);
    RUN_TEST(test_epoch_leap_year_feb_29_2024);
    RUN_TEST(test_epoch_non_leap_year_2025);
    RUN_TEST(test_epoch_end_of_day);
    RUN_TEST(test_epoch_december_31);
    RUN_TEST(test_epoch_invalid_year_zero);
    RUN_TEST(test_epoch_invalid_year_2023);
    RUN_TEST(test_epoch_invalid_month_zero);
    RUN_TEST(test_epoch_invalid_day_zero);
    RUN_TEST(test_epoch_century_not_leap);
    return UNITY_END();
}
