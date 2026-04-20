#include <unity.h>
#include <math.h>
#include <stdlib.h>
#include "util/slippy.h"

using namespace mclite;

// Reference values computed from the standard OSM slippy-map formula,
// cross-checked against tilenames.py at https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames

void test_null_island_zoom0() {
    // (0, 0) at z=0 is the single world tile; centre is (0.5, 0.5).
    TileFrac f = latLonToTileXY(0.0, 0.0, 0);
    TEST_ASSERT_TRUE(fabs(f.x - 0.5) < 1e-6);
    TEST_ASSERT_TRUE(fabs(f.y - 0.5) < 1e-6);
}

void test_null_island_zoom10() {
    // At z=10, tile count is 1024. (0, 0) -> (512, 512).
    TileFrac f = latLonToTileXY(0.0, 0.0, 10);
    TEST_ASSERT_TRUE(fabs(f.x - 512.0) < 1e-4);
    TEST_ASSERT_TRUE(fabs(f.y - 512.0) < 1e-4);
}

void test_london_zoom13_tile_x() {
    // Trafalgar Square ~ 51.5074, -0.1278 at z=13 -> tile x=4093
    TileFrac f = latLonToTileXY(51.5074, -0.1278, 13);
    TEST_ASSERT_EQUAL_INT(4093, (int)floor(f.x));
}

void test_sydney_zoom13_tile_x() {
    // Sydney Opera House ~ -33.8568, 151.2153 at z=13 -> tile x=7536
    TileFrac f = latLonToTileXY(-33.8568, 151.2153, 13);
    TEST_ASSERT_EQUAL_INT(7536, (int)floor(f.x));
}

void test_southern_hemisphere_y_increases() {
    // Y tile index grows as latitude decreases (northward = 0, southward = max).
    TileFrac fNorth = latLonToTileXY( 30.0, 0.0, 10);
    TileFrac fSouth = latLonToTileXY(-30.0, 0.0, 10);
    TEST_ASSERT_TRUE(fSouth.y > fNorth.y);
    // Both should be within valid range
    TEST_ASSERT_TRUE(fNorth.y > 0 && fNorth.y < 1024);
    TEST_ASSERT_TRUE(fSouth.y > 0 && fSouth.y < 1024);
}

void test_clamps_polar_lat() {
    // Above web-mercator limit should clamp, not blow up.
    TileFrac f = latLonToTileXY(89.9, 0.0, 10);
    TEST_ASSERT_TRUE(isfinite(f.y));
    TEST_ASSERT_TRUE(f.y >= -1.0);          // allow small float slack at the clamp
    TEST_ASSERT_TRUE(f.y < 1024.0);
    // Same for southern pole
    TileFrac fS = latLonToTileXY(-89.9, 0.0, 10);
    TEST_ASSERT_TRUE(isfinite(fS.y));
    TEST_ASSERT_TRUE(fS.y <= 1025.0);
    TEST_ASSERT_TRUE(fS.y > 0.0);
}

void test_meters_per_pixel_equator() {
    // At equator, z=0 -> 156543 m/px; z=1 -> 78271 m/px.
    double mpp0 = metersPerPixel(0.0, 0);
    double mpp1 = metersPerPixel(0.0, 1);
    TEST_ASSERT_TRUE(fabs(mpp0 - 156543.03) < 1.0);
    TEST_ASSERT_TRUE(fabs(mpp1 -  78271.51) < 1.0);
}

void test_meters_per_pixel_60deg() {
    // At 60deg latitude, mpp scales by cos(60) = 0.5 vs equator.
    double mppEq = metersPerPixel(0.0,  10);
    double mpp60 = metersPerPixel(60.0, 10);
    TEST_ASSERT_TRUE(fabs(mpp60 - mppEq * 0.5) < mppEq * 0.001);
}

void test_tile_count_powers() {
    TEST_ASSERT_EQUAL_INT(1,     slippyTileCount(0));
    TEST_ASSERT_EQUAL_INT(1024,  slippyTileCount(10));
    TEST_ASSERT_EQUAL_INT(65536, slippyTileCount(16));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_null_island_zoom0);
    RUN_TEST(test_null_island_zoom10);
    RUN_TEST(test_london_zoom13_tile_x);
    RUN_TEST(test_sydney_zoom13_tile_x);
    RUN_TEST(test_southern_hemisphere_y_increases);
    RUN_TEST(test_clamps_polar_lat);
    RUN_TEST(test_meters_per_pixel_equator);
    RUN_TEST(test_meters_per_pixel_60deg);
    RUN_TEST(test_tile_count_powers);
    return UNITY_END();
}
