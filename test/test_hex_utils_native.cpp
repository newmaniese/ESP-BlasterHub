#include <unity.h>
#include "Arduino.h"
#include "IrSender.h"
#include "IRsend.h"
#include "hex_utils.h"
#include <stddef.h> // for NULL

unsigned long mock_millis = 0;

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

void test_IrSender_isActive_basic(void) {
  IRsend mockIr;
  IrSender sender(mockIr);

  TEST_ASSERT_FALSE(sender.isActive());

  sender.queue(0x12345678, 32, 2);
  TEST_ASSERT_FALSE(sender.isActive());

  sender.loop();
  TEST_ASSERT_TRUE(sender.isActive());
  TEST_ASSERT_EQUAL(1, mockIr.sendCount);

  sender.loop();
  TEST_ASSERT_TRUE(sender.isActive());
  TEST_ASSERT_EQUAL(1, mockIr.sendCount);

  mock_millis += 60;
  sender.loop();
  TEST_ASSERT_FALSE(sender.isActive());
  TEST_ASSERT_EQUAL(2, mockIr.sendCount);
}

void test_IrSender_interruption(void) {
  IRsend mockIr;
  IrSender sender(mockIr);

  sender.queue(0xAAAA, 16, 10);
  sender.loop();
  TEST_ASSERT_TRUE(sender.isActive());
  TEST_ASSERT_EQUAL(1, mockIr.sendCount);
  TEST_ASSERT_EQUAL(0xAAAA, mockIr.lastData);

  sender.queue(0xBBBB, 16, 1);
  sender.loop();
  TEST_ASSERT_FALSE(sender.isActive());
  TEST_ASSERT_EQUAL(2, mockIr.sendCount);
  TEST_ASSERT_EQUAL(0xBBBB, mockIr.lastData);
}

void setUp(void) { mock_millis = 0; }
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_isHexValue_valid);
  RUN_TEST(test_isHexValue_invalid);
  RUN_TEST(test_isHexValue_empty);
  RUN_TEST(test_isHexValue_null);
  RUN_TEST(test_isHexValue_prefix);
  RUN_TEST(test_parseHex32_valid);
  RUN_TEST(test_parseHex32_invalid);
  RUN_TEST(test_IrSender_isActive_basic);
  RUN_TEST(test_IrSender_interruption);
  return UNITY_END();
}
