#include <unity.h>
#include "util/mgrs.h"

using namespace mclite;

// Reference points verified against external MGRS converters

void test_mgrs_statue_of_liberty() {
    // 40.6892 N, -74.0445 W -> 18T WL 8084 0656 (precision 4)
    String mgrs = latLonToMGRS(40.6892, -74.0445, 4);
    TEST_ASSERT_TRUE(mgrs.startsWith("18T WL"));
}

void test_mgrs_london_eye() {
    // 51.5033 N, -0.1195 W -> 30U XC
    String mgrs = latLonToMGRS(51.5033, -0.1195, 4);
    TEST_ASSERT_TRUE(mgrs.startsWith("30U"));
}

void test_mgrs_equator_prime_meridian() {
    // 0, 0 -> 31N AA
    String mgrs = latLonToMGRS(0.0, 0.0, 4);
    TEST_ASSERT_TRUE(mgrs.startsWith("31N"));
}

void test_mgrs_southern_hemisphere() {
    // Sydney: -33.8688, 151.2093 -> 56H
    String mgrs = latLonToMGRS(-33.8688, 151.2093, 4);
    TEST_ASSERT_TRUE(mgrs.startsWith("56H"));
}

void test_mgrs_norway_exception() {
    // Bergen: 60.39, 5.32 -> should be zone 32 (Norway exception)
    int zone = utmZoneNumber(60.39, 5.32);
    TEST_ASSERT_EQUAL_INT(32, zone);
}

void test_mgrs_svalbard_exception() {
    // Svalbard: 78.0, 16.0 -> should be zone 33
    int zone = utmZoneNumber(78.0, 16.0);
    TEST_ASSERT_EQUAL_INT(33, zone);
}

void test_mgrs_outside_utm() {
    String mgrs = latLonToMGRS(-85.0, 0.0, 4);
    TEST_ASSERT_EQUAL_STRING("Outside UTM", mgrs.c_str());
}

void test_mgrs_band_letter_equator() {
    TEST_ASSERT_EQUAL_CHAR('N', utmBandLetter(0.0));
}

void test_mgrs_band_letter_south() {
    TEST_ASSERT_EQUAL_CHAR('C', utmBandLetter(-79.0));
}

void test_mgrs_precision_1() {
    // Low precision — just check it doesn't crash and format looks right
    String mgrs = latLonToMGRS(48.8566, 2.3522, 1);
    TEST_ASSERT_TRUE(mgrs.length() > 0);
    TEST_ASSERT_TRUE(mgrs.indexOf(" ") > 0);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_mgrs_statue_of_liberty);
    RUN_TEST(test_mgrs_london_eye);
    RUN_TEST(test_mgrs_equator_prime_meridian);
    RUN_TEST(test_mgrs_southern_hemisphere);
    RUN_TEST(test_mgrs_norway_exception);
    RUN_TEST(test_mgrs_svalbard_exception);
    RUN_TEST(test_mgrs_outside_utm);
    RUN_TEST(test_mgrs_band_letter_equator);
    RUN_TEST(test_mgrs_band_letter_south);
    RUN_TEST(test_mgrs_precision_1);
    return UNITY_END();
}
