#include <unity.h>
#include "hex_utils.h"
#include <stddef.h> // for NULL

void test_isHexValue_valid(void) {
  TEST_ASSERT_TRUE(isHexValue("0123456789ABCDEF"));
  TEST_ASSERT_TRUE(isHexValue("abcdef"));
  TEST_ASSERT_TRUE(isHexValue("0"));
  TEST_ASSERT_TRUE(isHexValue("A"));
  TEST_ASSERT_TRUE(isHexValue("f"));
}

void test_isHexValue_invalid(void) {
  TEST_ASSERT_FALSE(isHexValue("G"));
  TEST_ASSERT_FALSE(isHexValue("123G"));
  TEST_ASSERT_FALSE(isHexValue(" "));
  TEST_ASSERT_FALSE(isHexValue("-1"));
  TEST_ASSERT_FALSE(isHexValue("."));
}

void test_isHexValue_empty(void) {
  TEST_ASSERT_FALSE(isHexValue(""));
}

void test_isHexValue_null(void) {
  TEST_ASSERT_FALSE(isHexValue(NULL));
}

void test_isHexValue_prefix(void) {
  // Should reject 0x prefix
  TEST_ASSERT_FALSE(isHexValue("0x123"));
  TEST_ASSERT_FALSE(isHexValue("0XABC"));
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_isHexValue_valid);
    RUN_TEST(test_isHexValue_invalid);
    RUN_TEST(test_isHexValue_empty);
    RUN_TEST(test_isHexValue_null);
    RUN_TEST(test_isHexValue_prefix);
    return UNITY_END();
}
