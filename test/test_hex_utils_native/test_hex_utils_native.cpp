#include <unity.h>
#include "Arduino.h"
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
  TEST_ASSERT_FALSE(isHexValue("123 45"));
  TEST_ASSERT_FALSE(isHexValue("ABC "));
  TEST_ASSERT_FALSE(isHexValue(" ABC"));
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

void test_isHexValue_edge_cases(void) {
  // ASCII boundaries outside valid hex ranges
  TEST_ASSERT_FALSE(isHexValue("/")); // just before '0'
  TEST_ASSERT_FALSE(isHexValue(":")); // just after '9'
  TEST_ASSERT_FALSE(isHexValue("@")); // just before 'A'
  TEST_ASSERT_FALSE(isHexValue("G")); // just after 'F'
  TEST_ASSERT_FALSE(isHexValue("`")); // just before 'a'
  TEST_ASSERT_FALSE(isHexValue("g")); // just after 'f'

  // Extended ASCII / negative signed chars
  TEST_ASSERT_FALSE(isHexValue("\x80"));
  TEST_ASSERT_FALSE(isHexValue("\xFF"));

  // Long valid hex string
  TEST_ASSERT_TRUE(isHexValue("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"));
}

void test_parseHex32_valid(void) {
  uint32_t val = 0;
  TEST_ASSERT_TRUE(parseHex32("FF827D", val));
  TEST_ASSERT_EQUAL_UINT32(0xFF827D, val);

  TEST_ASSERT_TRUE(parseHex32("FFFFFFFF", val));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, val);

  TEST_ASSERT_TRUE(parseHex32("0", val));
  TEST_ASSERT_EQUAL_UINT32(0, val);
}

void test_parseHex32_invalid(void) {
  uint32_t val = 0;
  // Exceeds 32-bit max
  TEST_ASSERT_FALSE(parseHex32("100000000", val));
  TEST_ASSERT_FALSE(parseHex32("FFFFFFFFFFFF", val));

  // Trailing garbage / invalid chars
  TEST_ASSERT_FALSE(parseHex32("FF827DG", val));
  TEST_ASSERT_FALSE(parseHex32("0xFF827D", val));

  // Empty/null
  TEST_ASSERT_FALSE(parseHex32("", val));
  TEST_ASSERT_FALSE(parseHex32(NULL, val));
}

void test_uint64ToHex(void) {
  TEST_ASSERT_EQUAL_STRING("00000000", uint64ToHex(0).c_str());
  TEST_ASSERT_EQUAL_STRING("0000000A", uint64ToHex(10).c_str());
  TEST_ASSERT_EQUAL_STRING("000000FF", uint64ToHex(255).c_str());
  TEST_ASSERT_EQUAL_STRING("00001234", uint64ToHex(0x1234).c_str());
  TEST_ASSERT_EQUAL_STRING("12345678", uint64ToHex(0x12345678).c_str());
  TEST_ASSERT_EQUAL_STRING("FFFFFFFF", uint64ToHex(0xFFFFFFFF).c_str());

  // Truncation of upper 32 bits
  TEST_ASSERT_EQUAL_STRING("12345678", uint64ToHex(0x9999999912345678ULL).c_str());
  TEST_ASSERT_EQUAL_STRING("00000000", uint64ToHex(0xFFFFFFFF00000000ULL).c_str());
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
  RUN_TEST(test_isHexValue_edge_cases);
  RUN_TEST(test_parseHex32_valid);
  RUN_TEST(test_parseHex32_invalid);
  RUN_TEST(test_uint64ToHex);
  return UNITY_END();
}
