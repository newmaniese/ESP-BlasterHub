#include <unity.h>
#include "Arduino.h"
#include "IrSender.h"
#include "IRsend.h"
#include "hex_utils.h"
#include <stddef.h> // for NULL
#include "ble_schedule.h"

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

void test_calculateScheduleCountdown_happy_path(void) {
  uint32_t seconds_remaining = 0;
  char command_name[32] = "";

  bool result = calculateScheduleCountdown(
    true, 5000, 1000, "TurnOnTV", 2000,
    &seconds_remaining, command_name, sizeof(command_name)
  );

  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL_UINT32(4, seconds_remaining); // 5000 - (2000 - 1000) = 4000ms -> 4s
  TEST_ASSERT_EQUAL_STRING("TurnOnTV", command_name);
}

void test_calculateScheduleCountdown_rounding(void) {
  uint32_t seconds_remaining = 0;
  char command_name[32] = "";

  bool result = calculateScheduleCountdown(
    true, 5000, 1000, "VolUp", 2500, // elapsed = 1500, remaining = 3500ms
    &seconds_remaining, command_name, sizeof(command_name)
  );

  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL_UINT32(4, seconds_remaining); // (3500 + 999) / 1000 = 4s
  TEST_ASSERT_EQUAL_STRING("VolUp", command_name);
}

void test_calculateScheduleCountdown_unarmed(void) {
  uint32_t seconds_remaining = 0;
  char command_name[32] = "";

  bool result = calculateScheduleCountdown(
    false, 5000, 1000, "TurnOnTV", 2000,
    &seconds_remaining, command_name, sizeof(command_name)
  );

  TEST_ASSERT_FALSE(result);
}

void test_calculateScheduleCountdown_empty_command(void) {
  uint32_t seconds_remaining = 0;
  char command_name[32] = "";

  bool result = calculateScheduleCountdown(
    true, 5000, 1000, "", 2000,
    &seconds_remaining, command_name, sizeof(command_name)
  );

  TEST_ASSERT_FALSE(result);
}

void test_calculateScheduleCountdown_null_pointers(void) {
  uint32_t seconds_remaining = 0;
  char command_name[32] = "";

  // Null seconds_remaining
  TEST_ASSERT_FALSE(calculateScheduleCountdown(
    true, 5000, 1000, "Cmd", 2000,
    NULL, command_name, sizeof(command_name)
  ));

  // Null command_name out buffer
  TEST_ASSERT_FALSE(calculateScheduleCountdown(
    true, 5000, 1000, "Cmd", 2000,
    &seconds_remaining, NULL, sizeof(command_name)
  ));

  // Null commandName input
  TEST_ASSERT_FALSE(calculateScheduleCountdown(
    true, 5000, 1000, NULL, 2000,
    &seconds_remaining, command_name, sizeof(command_name)
  ));
}

void test_calculateScheduleCountdown_zero_max_name(void) {
  uint32_t seconds_remaining = 0;
  char command_name[32] = "";

  bool result = calculateScheduleCountdown(
    true, 5000, 1000, "Cmd", 2000,
    &seconds_remaining, command_name, 0
  );

  TEST_ASSERT_FALSE(result);
}

void test_calculateScheduleCountdown_expired(void) {
  uint32_t seconds_remaining = 0;
  char command_name[32] = "";

  bool result = calculateScheduleCountdown(
    true, 5000, 1000, "Cmd", 6000, // elapsed = 5000 >= delayMs
    &seconds_remaining, command_name, sizeof(command_name)
  );

  TEST_ASSERT_FALSE(result);
}

void test_calculateScheduleCountdown_truncation(void) {
  uint32_t seconds_remaining = 0;
  char command_name[5] = ""; // Very small buffer

  bool result = calculateScheduleCountdown(
    true, 5000, 1000, "LongCommand", 2000,
    &seconds_remaining, command_name, sizeof(command_name)
  );

  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL_STRING("Long", command_name); // Buffer size 5 means 4 chars + null terminator
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_isHexValue_valid);
  RUN_TEST(test_isHexValue_invalid);
  RUN_TEST(test_isHexValue_empty);
  RUN_TEST(test_isHexValue_null);
  RUN_TEST(test_isHexValue_prefix);
  RUN_TEST(test_parseHex32_valid);
  RUN_TEST(test_parseHex32_invalid);
  RUN_TEST(test_uint64ToHex);
  RUN_TEST(test_IrSender_isActive_basic);
  RUN_TEST(test_IrSender_interruption);

  RUN_TEST(test_calculateScheduleCountdown_happy_path);
  RUN_TEST(test_calculateScheduleCountdown_rounding);
  RUN_TEST(test_calculateScheduleCountdown_unarmed);
  RUN_TEST(test_calculateScheduleCountdown_empty_command);
  RUN_TEST(test_calculateScheduleCountdown_null_pointers);
  RUN_TEST(test_calculateScheduleCountdown_zero_max_name);
  RUN_TEST(test_calculateScheduleCountdown_expired);
  RUN_TEST(test_calculateScheduleCountdown_truncation);

  return UNITY_END();
}
