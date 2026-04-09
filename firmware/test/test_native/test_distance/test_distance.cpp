#include <unity.h>
#include "util/distance.h"

using namespace mclite;

void test_haversine_same_point() {
    double d = haversineMeters(48.8566, 2.3522, 48.8566, 2.3522);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, (float)d);
}

void test_haversine_paris_london() {
    // Paris to London ~343 km
    double d = haversineMeters(48.8566, 2.3522, 51.5074, -0.1278);
    TEST_ASSERT_FLOAT_WITHIN(5000.0f, 343000.0f, (float)d);
}

void test_haversine_new_york_los_angeles() {
    // ~3944 km
    double d = haversineMeters(40.7128, -74.0060, 34.0522, -118.2437);
    TEST_ASSERT_FLOAT_WITHIN(50000.0f, 3944000.0f, (float)d);
}

void test_haversine_antipodal() {
    // Opposite sides of earth ~20015 km
    double d = haversineMeters(0.0, 0.0, 0.0, 180.0);
    TEST_ASSERT_FLOAT_WITHIN(100000.0f, 20015000.0f, (float)d);
}

void test_format_distance_meters() {
    String s = formatDistance(500.0);
    TEST_ASSERT_EQUAL_STRING("500 m", s.c_str());
}

void test_format_distance_km() {
    String s = formatDistance(2500.0);
    TEST_ASSERT_EQUAL_STRING("2.5 km", s.c_str());
}

void test_format_distance_threshold() {
    String s = formatDistance(1000.0);
    TEST_ASSERT_EQUAL_STRING("1.0 km", s.c_str());
}

void test_format_distance_below_threshold() {
    String s = formatDistance(999.0);
    TEST_ASSERT_EQUAL_STRING("999 m", s.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_haversine_same_point);
    RUN_TEST(test_haversine_paris_london);
    RUN_TEST(test_haversine_new_york_los_angeles);
    RUN_TEST(test_haversine_antipodal);
    RUN_TEST(test_format_distance_meters);
    RUN_TEST(test_format_distance_km);
    RUN_TEST(test_format_distance_threshold);
    RUN_TEST(test_format_distance_below_threshold);
    return UNITY_END();
}
