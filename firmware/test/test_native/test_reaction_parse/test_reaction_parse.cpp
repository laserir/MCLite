// Tests for the inbound reaction parser (util/ReactionParse.h).
//
// The stakes here are asymmetric. A reaction that fails to parse is a missing
// chip. A NORMAL MESSAGE that parses as a reaction is destroyed: UIManager
// returns before addMessage(), so there is no bubble, no unread badge, no
// notification, no SD write and no log entry. On an emergency communicator that
// is the worst failure mode available, and the path is not gated by
// messaging.reactions, so it runs on every device. Most of these tests are
// therefore about what must NOT parse.
#include <unity.h>
#include "util/ReactionParse.h"

using namespace mclite;

// UTF-8 literals, spelled out so the test file stays ASCII.
static const char* THUMBSUP = "\xF0\x9F\x91\x8D";              // U+1F44D
static const char* HEART    = "\xE2\x9D\xA4";                  // U+2764
static const char* HEART_VS = "\xE2\x9D\xA4\xEF\xB8\x8F";      // U+2764 U+FE0F
static const char* GRIN     = "\xF0\x9F\x98\x80";              // U+1F600
static const char* UUML     = "\xC3\x9C";                      // U+00DC (U-umlaut)
static const char* KEYCAP1  = "1\xEF\xB8\x8F\xE2\x83\xA3";        // "1" VS16 U+20E3
static const char* FAMILY   = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"
                              "\xE2\x80\x8D\xF0\x9F\x91\xA7";     // man ZWJ woman ZWJ girl
static const char* FLAG_DE  = "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA";  // regional indicators D+E
static const char* THUMB_ST = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";  // thumbs up + skin tone
static const char* CJK      = "\xE6\x97\xA9\xE5\xAE\x89";            // CJK "good morning"

static String E, H;

// ═══ What must parse ═══

void test_dm_reaction_parses() {
    TEST_ASSERT_TRUE(parseIncomingReaction(String(THUMBSUP) + "\nba6ma8jy", false, E, H));
    TEST_ASSERT_EQUAL_STRING(THUMBSUP, E.c_str());
    TEST_ASSERT_EQUAL_STRING("ba6ma8jy", H.c_str());
}

void test_channel_reaction_parses() {
    TEST_ASSERT_TRUE(parseIncomingReaction(String(HEART) + "@[Alice]\nba6ma8jy", true, E, H));
    TEST_ASSERT_EQUAL_STRING(HEART, E.c_str());
}

// MeshCore One sends the heart with a variation selector; two codepoints must pass.
void test_variation_selector_parses() {
    TEST_ASSERT_TRUE(parseIncomingReaction(String(HEART_VS) + "\nba6ma8jy", false, E, H));
}

// Inbound hashes may be uppercase or use Crockford's O/I/L aliases.
void test_hash_is_normalized() {
    TEST_ASSERT_TRUE(parseIncomingReaction(String(HEART) + "\nBA6MA8JY", false, E, H));
    TEST_ASSERT_EQUAL_STRING("ba6ma8jy", H.c_str());
}

// ═══ What must NOT parse — these are the message-destroying cases ═══

// The regression that motivated this file: "midnight" is legal Crockford
// (i->1, o->0, no u) and the prefix starts with a non-ASCII byte.
void test_emoji_led_sentence_is_not_a_reaction() {
    TEST_ASSERT_FALSE(parseIncomingReaction(String(GRIN) + " party at\nmidnight", false, E, H));
}

// A German message opening with an umlaut and ending in an 8-letter word.
void test_accented_message_is_not_a_reaction() {
    TEST_ASSERT_FALSE(parseIncomingReaction(String(UUML) + "berfall\nBahnhof2", false, E, H));
}

// A single ASCII byte anywhere in the prefix disqualifies it.
void test_prefix_with_trailing_space_rejected() {
    TEST_ASSERT_FALSE(parseIncomingReaction(String(THUMBSUP) + " \nba6ma8jy", false, E, H));
}

void test_ascii_prefix_rejected() {
    TEST_ASSERT_FALSE(parseIncomingReaction("ok\nba6ma8jy", false, E, H));
}

// Bounds what a peer can push into the stored reaction list.
void test_too_many_codepoints_rejected() {
    String many = String(GRIN) + GRIN + GRIN + GRIN + GRIN + GRIN + GRIN + GRIN + GRIN;  // 9
    TEST_ASSERT_FALSE(parseIncomingReaction(many + "\nba6ma8jy", false, E, H));
}

// ═══ Real emoji the old "every byte non-ASCII, max 4 codepoints" rule rejected ═══

// Keycaps lead with an ASCII digit, so a byte-based test could never accept them.
void test_keycap_parses() {
    TEST_ASSERT_TRUE(parseIncomingReaction(String(KEYCAP1) + "\nba6ma8jy", false, E, H));
}

// ZWJ families run to 5+ codepoints.
void test_zwj_family_parses() {
    TEST_ASSERT_TRUE(parseIncomingReaction(String(FAMILY) + "\nba6ma8jy", false, E, H));
}

void test_flag_parses() {
    TEST_ASSERT_TRUE(parseIncomingReaction(String(FLAG_DE) + "\nba6ma8jy", false, E, H));
}

void test_skin_tone_parses() {
    TEST_ASSERT_TRUE(parseIncomingReaction(String(THUMB_ST) + "\nba6ma8jy", false, E, H));
}

// ═══ Non-Latin messages the byte-based rule still swallowed ═══

// Every byte is non-ASCII, so the previous rule consumed this and destroyed it.
void test_cjk_message_is_not_a_reaction() {
    TEST_ASSERT_FALSE(parseIncomingReaction(String(CJK) + "\n20260829", false, E, H));
}

// A bare ASCII digit run is not a keycap without the combining mark.
void test_digits_without_keycap_mark_rejected() {
    TEST_ASSERT_FALSE(parseIncomingReaction("1\nba6ma8jy", false, E, H));
}

void test_empty_prefix_rejected() {
    TEST_ASSERT_FALSE(parseIncomingReaction("\nba6ma8jy", false, E, H));
}

void test_no_newline_rejected() {
    TEST_ASSERT_FALSE(parseIncomingReaction(String(THUMBSUP) + "ba6ma8jy", false, E, H));
}

// 'u' is excluded from Crockford, so this is an ordinary message.
void test_non_crockford_last_line_rejected() {
    TEST_ASSERT_FALSE(parseIncomingReaction(String(THUMBSUP) + "\nbausma8j", false, E, H));
}

void test_wrong_hash_length_rejected() {
    TEST_ASSERT_FALSE(parseIncomingReaction(String(THUMBSUP) + "\nba6ma8j", false, E, H));
    TEST_ASSERT_FALSE(parseIncomingReaction(String(THUMBSUP) + "\nba6ma8jyz", false, E, H));
}

// Channel form requires the @[name] envelope; without it a channel post that
// merely looks reaction-shaped must survive.
void test_channel_without_bracket_rejected() {
    TEST_ASSERT_FALSE(parseIncomingReaction(String(HEART) + "\nba6ma8jy", true, E, H));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_dm_reaction_parses);
    RUN_TEST(test_channel_reaction_parses);
    RUN_TEST(test_variation_selector_parses);
    RUN_TEST(test_hash_is_normalized);
    RUN_TEST(test_emoji_led_sentence_is_not_a_reaction);
    RUN_TEST(test_accented_message_is_not_a_reaction);
    RUN_TEST(test_prefix_with_trailing_space_rejected);
    RUN_TEST(test_ascii_prefix_rejected);
    RUN_TEST(test_too_many_codepoints_rejected);
    RUN_TEST(test_keycap_parses);
    RUN_TEST(test_zwj_family_parses);
    RUN_TEST(test_flag_parses);
    RUN_TEST(test_skin_tone_parses);
    RUN_TEST(test_cjk_message_is_not_a_reaction);
    RUN_TEST(test_digits_without_keycap_mark_rejected);
    RUN_TEST(test_empty_prefix_rejected);
    RUN_TEST(test_no_newline_rejected);
    RUN_TEST(test_non_crockford_last_line_rejected);
    RUN_TEST(test_wrong_hash_length_rejected);
    RUN_TEST(test_channel_without_bracket_rejected);
    return UNITY_END();
}
