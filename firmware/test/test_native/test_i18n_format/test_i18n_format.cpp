#include <Arduino.h>
#include <unity.h>

// I18n::init() walks the SD card for available languages; these tests never call
// it, but the translation unit needs the symbol. The stub File is always falsey,
// so the scan would no-op anyway.
#include <SD.h>
struct SDStub { File open(const char*) { return File(); } };
static SDStub SD;

// formatSpecsMatch / englishFor are private, and the tests fake a loaded
// translation by writing _entries directly -- which is exactly what a stale lang
// file on the SD card does at runtime.
#define private public
#include "i18n/I18n.h"
#undef private
#include "i18n/I18n.cpp"

// I18n reads the lang JSON through SDCard; these tests never load one.
namespace mclite {
    SDCard& SDCard::instance() { static SDCard inst; return inst; }
    bool SDCard::fileExists(const char*) { return false; }
    String SDCard::readFile(const char*, size_t) { return ""; }
}

using namespace mclite;

void setUp() {
    I18n::instance()._count = 0;
}
void tearDown() {}

// Pretend the SD card supplied `value` for `key`, as a lang file would.
static void fakeTranslation(const char* key, const char* value) {
    I18n::instance()._entries[0] = { key, value };
    I18n::instance()._count = 1;
}

// ═══ formatSpecsMatch ═══

void test_identical_formats_match() {
    TEST_ASSERT_TRUE(I18n::formatSpecsMatch("Switch to %s MHz", "Auf %s MHz wechseln"));
}
void test_no_specifiers_either_side_matches() {
    TEST_ASSERT_TRUE(I18n::formatSpecsMatch("Cancel", "Abbrechen"));
}
void test_different_conversion_is_a_mismatch() {
    // The exact case that shipped: firmware moved %d -> %s, the SD file did not.
    TEST_ASSERT_FALSE(I18n::formatSpecsMatch("Auf %d MHz", "Switch to %s MHz"));
}
void test_missing_specifier_is_a_mismatch() {
    TEST_ASSERT_FALSE(I18n::formatSpecsMatch("Auf MHz wechseln", "Switch to %s MHz"));
}
void test_extra_specifier_is_a_mismatch() {
    TEST_ASSERT_FALSE(I18n::formatSpecsMatch("%s auf %s MHz", "Switch to %s MHz"));
}
void test_order_matters() {
    TEST_ASSERT_FALSE(I18n::formatSpecsMatch("%d von %s", "%s of %d"));
}
void test_precision_is_part_of_the_specifier() {
    TEST_ASSERT_TRUE(I18n::formatSpecsMatch("%.3f MHz", "%.3f MHz"));
    TEST_ASSERT_FALSE(I18n::formatSpecsMatch("%f MHz", "%.3f MHz"));
}
void test_literal_percent_is_not_a_specifier() {
    TEST_ASSERT_TRUE(I18n::formatSpecsMatch("Akku %d%%", "Battery %d%%"));
    TEST_ASSERT_TRUE(I18n::formatSpecsMatch("100%% geladen", "100%% charged"));
}
void test_percent_literal_on_one_side_only_still_matches_specifiers() {
    // "%%" carries no argument, so its presence on one side is not a type error.
    TEST_ASSERT_TRUE(I18n::formatSpecsMatch("%d%% voll", "%d full"));
}

// ═══ tf(): the guard callers actually use ═══

void test_tf_returns_a_matching_translation() {
    fakeTranslation("offgrid_confirm_on_body", "Auf %s MHz wechseln.");
    TEST_ASSERT_EQUAL_STRING("Auf %s MHz wechseln.", tf("offgrid_confirm_on_body"));
}

void test_tf_rejects_a_stale_translation_and_falls_back_to_english() {
    // A device flashed with new firmware whose SD lang files are still the old
    // ones. Before this guard, snprintf printed the char* argument through %d --
    // a long meaningless number in the dialog.
    fakeTranslation("offgrid_confirm_on_body", "Auf %d MHz wechseln.");
    const char* got = tf("offgrid_confirm_on_body");
    TEST_ASSERT_EQUAL_STRING(I18n::englishFor("offgrid_confirm_on_body"), got);
    TEST_ASSERT_NOT_NULL(strstr(got, "%s"));
}

void test_tf_rejects_the_dangerous_direction_too() {
    // The reverse (a %s reading an int) is a crash, not cosmetic: newer lang
    // files on older firmware.
    fakeTranslation("sos_sent", "%s Empfaenger");
    TEST_ASSERT_EQUAL_STRING(I18n::englishFor("sos_sent"), tf("sos_sent"));
}

void test_tf_passes_through_an_untranslated_key() {
    TEST_ASSERT_EQUAL_STRING(I18n::englishFor("sos_sent"), tf("sos_sent"));
}

void test_tf_on_an_unknown_key_matches_t() {
    TEST_ASSERT_EQUAL_STRING(t("no_such_key_at_all"), tf("no_such_key_at_all"));
}

// Every English default that takes arguments is the reference the guard compares
// against, so a malformed one would break the fallback itself.
void test_english_defaults_have_parseable_specifiers() {
    for (size_t i = 0; DEFAULT_STRINGS[i].key != nullptr; i++) {
        TEST_ASSERT_TRUE_MESSAGE(I18n::formatSpecsMatch(DEFAULT_STRINGS[i].en, DEFAULT_STRINGS[i].en),
                                 DEFAULT_STRINGS[i].key);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_identical_formats_match);
    RUN_TEST(test_no_specifiers_either_side_matches);
    RUN_TEST(test_different_conversion_is_a_mismatch);
    RUN_TEST(test_missing_specifier_is_a_mismatch);
    RUN_TEST(test_extra_specifier_is_a_mismatch);
    RUN_TEST(test_order_matters);
    RUN_TEST(test_precision_is_part_of_the_specifier);
    RUN_TEST(test_literal_percent_is_not_a_specifier);
    RUN_TEST(test_percent_literal_on_one_side_only_still_matches_specifiers);
    RUN_TEST(test_tf_returns_a_matching_translation);
    RUN_TEST(test_tf_rejects_a_stale_translation_and_falls_back_to_english);
    RUN_TEST(test_tf_rejects_the_dangerous_direction_too);
    RUN_TEST(test_tf_passes_through_an_untranslated_key);
    RUN_TEST(test_tf_on_an_unknown_key_matches_t);
    RUN_TEST(test_english_defaults_have_parseable_specifiers);
    return UNITY_END();
}
