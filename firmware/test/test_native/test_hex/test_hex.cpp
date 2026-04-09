#include <unity.h>
#include "util/hex.h"

using namespace mclite;

void test_hex_valid_lowercase()  { TEST_ASSERT_TRUE(isHexString("0123456789abcdef")); }
void test_hex_valid_uppercase()  { TEST_ASSERT_TRUE(isHexString("ABCDEF")); }
void test_hex_valid_mixed_case() { TEST_ASSERT_TRUE(isHexString("aAbBcC")); }
void test_hex_valid_digits_only(){ TEST_ASSERT_TRUE(isHexString("0123456789")); }
void test_hex_empty_string()     { TEST_ASSERT_FALSE(isHexString("")); }
void test_hex_invalid_char_g()   { TEST_ASSERT_FALSE(isHexString("abcg")); }
void test_hex_invalid_space()    { TEST_ASSERT_FALSE(isHexString("ab cd")); }
void test_hex_single_char()      { TEST_ASSERT_TRUE(isHexString("f")); }
void test_hex_32_char_key()      { TEST_ASSERT_TRUE(isHexString("0123456789abcdef0123456789abcdef")); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_hex_valid_lowercase);
    RUN_TEST(test_hex_valid_uppercase);
    RUN_TEST(test_hex_valid_mixed_case);
    RUN_TEST(test_hex_valid_digits_only);
    RUN_TEST(test_hex_empty_string);
    RUN_TEST(test_hex_invalid_char_g);
    RUN_TEST(test_hex_invalid_space);
    RUN_TEST(test_hex_single_char);
    RUN_TEST(test_hex_32_char_key);
    return UNITY_END();
}
