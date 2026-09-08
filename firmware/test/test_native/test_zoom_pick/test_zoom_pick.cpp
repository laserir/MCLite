#include <Arduino.h>
#include <unity.h>

// Pure rule, no LVGL or SD behind it — see the note in the header.
#include "util/zoom_pick.h"

using namespace mclite;

void setUp() {}
void tearDown() {}

// Every zoom in the pack has a centre tile — the common case.
static int pickAll(const std::vector<uint8_t>& z) {
    return pickZoomIdx(z, std::vector<bool>(z.size(), true));
}

// ═══ The ordinary case ═══

void test_full_pack_opens_at_the_preferred_zoom() {
    std::vector<uint8_t> z{6, 8, 10, 12, 14};
    TEST_ASSERT_EQUAL_UINT8(10, z[pickAll(z)]);
}

void test_exact_preferred_wins_over_neighbours() {
    std::vector<uint8_t> z{9, 10, 11};
    TEST_ASSERT_EQUAL_UINT8(10, z[pickAll(z)]);
}

// ═══ Packs with nothing at the preferred scale ═══

void test_city_pack_opens_at_its_coarsest_not_its_deepest() {
    // 11-14 is an ordinary city-pack download. Opening at 14 is the street-corner
    // problem this rule exists to avoid.
    std::vector<uint8_t> z{11, 12, 13, 14};
    TEST_ASSERT_EQUAL_UINT8(11, z[pickAll(z)]);
}

void test_coarse_and_deep_picks_the_nearer_deep_one() {
    // The mirror case: "deepest at or below preferred" would open this at
    // continent scale, when 12 is far closer to what was asked for.
    std::vector<uint8_t> z{5, 12};
    TEST_ASSERT_EQUAL_UINT8(12, z[pickAll(z)]);
}

void test_all_coarse_pack_opens_at_its_deepest() {
    std::vector<uint8_t> z{4, 6, 8};
    TEST_ASSERT_EQUAL_UINT8(8, z[pickAll(z)]);
}

void test_tie_goes_to_the_coarser_level() {
    // 8 and 12 are both two away; more ground is the safer error for an opening
    // view, and zooming in is one tap.
    std::vector<uint8_t> z{8, 12};
    TEST_ASSERT_EQUAL_UINT8(8, z[pickAll(z)]);
}

// ═══ Missing centre tiles ═══

void test_skips_zooms_without_a_centre_tile() {
    // Preferred level exists in the pack but has no tile here; 12 is next nearest.
    std::vector<uint8_t> z{6, 10, 12};
    std::vector<bool> has{false, false, true};
    TEST_ASSERT_EQUAL_UINT8(12, z[pickZoomIdx(z, has)]);
}

void test_single_usable_zoom_is_chosen_whatever_its_depth() {
    std::vector<uint8_t> z{6, 8, 10, 12};
    std::vector<bool> has{false, false, false, true};
    TEST_ASSERT_EQUAL_UINT8(12, z[pickZoomIdx(z, has)]);
}

void test_no_centre_tile_anywhere_falls_back_to_deepest() {
    // Matches the original behaviour: show something rather than nothing.
    std::vector<uint8_t> z{6, 8, 10};
    std::vector<bool> has{false, false, false};
    TEST_ASSERT_EQUAL_UINT8(10, z[pickZoomIdx(z, has)]);
}

// ═══ Degenerate inputs ═══

void test_single_zoom_pack() {
    std::vector<uint8_t> z{5};
    TEST_ASSERT_EQUAL_UINT8(5, z[pickAll(z)]);
}

void test_empty_pack_does_not_index_out_of_range() {
    // MapScreen::open() returns before this on an empty list, but the rule must
    // not be the thing that makes that a crash if the guard ever moves.
    std::vector<uint8_t> z{};
    TEST_ASSERT_EQUAL_INT(0, pickZoomIdx(z, std::vector<bool>{}));
}

void test_short_hasTile_vector_is_treated_as_missing() {
    std::vector<uint8_t> z{6, 8, 10};
    std::vector<bool> has{true};            // shorter than zooms
    TEST_ASSERT_EQUAL_UINT8(6, z[pickZoomIdx(z, has)]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_full_pack_opens_at_the_preferred_zoom);
    RUN_TEST(test_exact_preferred_wins_over_neighbours);
    RUN_TEST(test_city_pack_opens_at_its_coarsest_not_its_deepest);
    RUN_TEST(test_coarse_and_deep_picks_the_nearer_deep_one);
    RUN_TEST(test_all_coarse_pack_opens_at_its_deepest);
    RUN_TEST(test_tie_goes_to_the_coarser_level);
    RUN_TEST(test_skips_zooms_without_a_centre_tile);
    RUN_TEST(test_single_usable_zoom_is_chosen_whatever_its_depth);
    RUN_TEST(test_no_centre_tile_anywhere_falls_back_to_deepest);
    RUN_TEST(test_single_zoom_pack);
    RUN_TEST(test_empty_pack_does_not_index_out_of_range);
    RUN_TEST(test_short_hasTile_vector_is_treated_as_missing);
    return UNITY_END();
}
