#include <Arduino.h>
#include <unity.h>
#include <IRsend.h>
#include "ir_utils.h"
#include "IrSender.h"

// ---------------------------------------------------------------------------
// replayUrlFor
// ---------------------------------------------------------------------------

void test_replayUrlFor_nec_32bit(void) {
  IrCapture c;
  c.protocol = "NEC";
  c.value = 0xFF827D;
  c.bits = 32;
  c.human = "NEC 0xFF827D";

  String url = replayUrlFor(c);
  TEST_ASSERT_TRUE(url.startsWith("/send?type=nec&data="));
  TEST_ASSERT_TRUE(url.indexOf("00FF827D") >= 0);
  TEST_ASSERT_TRUE(url.endsWith("&length=32"));
}

void test_replayUrlFor_nec_16bit(void) {
  IrCapture c;
  c.protocol = "NEC";
  c.value = 0xABCD;
  c.bits = 16;

  String url = replayUrlFor(c);
  TEST_ASSERT_TRUE(url.startsWith("/send?type=nec&data="));
  TEST_ASSERT_TRUE(url.endsWith("&length=16"));
}

void test_replayUrlFor_non_nec_returns_empty(void) {
  IrCapture c;
  c.protocol = "Sony";
  c.value = 0x1234;
  c.bits = 12;

  String url = replayUrlFor(c);
  TEST_ASSERT_EQUAL_STRING("", url.c_str());
}

void test_replayUrlFor_nec_case_insensitive(void) {
  IrCapture c;
  c.protocol = "nec";
  c.value = 0x01;
  c.bits = 32;

  String url = replayUrlFor(c);
  TEST_ASSERT_TRUE(url.length() > 0);
}

// ---------------------------------------------------------------------------
// isHexValue
// ---------------------------------------------------------------------------

void test_isHexValue_valid(void) {
  TEST_ASSERT_TRUE(isHexValue("0123456789ABCDEF"));
  TEST_ASSERT_TRUE(isHexValue("abcdef"));
  TEST_ASSERT_TRUE(isHexValue("0"));
}

void test_isHexValue_invalid(void) {
  TEST_ASSERT_FALSE(isHexValue("G"));
  TEST_ASSERT_FALSE(isHexValue("123G"));
  TEST_ASSERT_FALSE(isHexValue(" "));
}

void test_isHexValue_empty(void) {
  TEST_ASSERT_FALSE(isHexValue(""));
}

void test_isHexValue_null(void) {
  TEST_ASSERT_FALSE(isHexValue(nullptr));
}

// ---------------------------------------------------------------------------
// saveUrlFor
// ---------------------------------------------------------------------------

void test_saveUrlFor_with_name(void) {
  IrCapture c;
  c.protocol = "NEC";
  c.value = 0xDEAD;
  c.bits = 32;

  String url = saveUrlFor(c, "Power");
  TEST_ASSERT_TRUE(url.startsWith("/save?protocol=NEC&value="));
  TEST_ASSERT_TRUE(url.indexOf("&length=32") >= 0);
  TEST_ASSERT_TRUE(url.indexOf("&name=Power") >= 0);
}

void test_saveUrlFor_without_name(void) {
  IrCapture c;
  c.protocol = "NEC";
  c.value = 0xBEEF;
  c.bits = 32;

  String url = saveUrlFor(c, "");
  TEST_ASSERT_TRUE(url.startsWith("/save?protocol=NEC&value="));
  TEST_ASSERT_TRUE(url.indexOf("&length=32") >= 0);
  // No name param when empty
  TEST_ASSERT_TRUE(url.indexOf("&name=") < 0);
}

void test_saveUrlFor_value_hex_format(void) {
  IrCapture c;
  c.protocol = "NEC";
  c.value = 0xFF;
  c.bits = 32;

  String url = saveUrlFor(c, "");
  // Value should be zero-padded to 8 hex chars
  TEST_ASSERT_TRUE(url.indexOf("000000FF") >= 0);
}

// ---------------------------------------------------------------------------
// IrSender
// ---------------------------------------------------------------------------

void test_ir_sender_queue_basic(void) {
  IRsend irsend(4);
  IrSender sender(irsend);

  TEST_ASSERT_FALSE(sender.isJobPending());
  sender.queue(0x1234, 32, 1);
  TEST_ASSERT_TRUE(sender.isJobPending());
  TEST_ASSERT_FALSE(sender.isActive());
}

void test_ir_sender_queue_invalid(void) {
  IRsend irsend(4);
  IrSender sender(irsend);

  // repeat < 1 should not queue a job
  sender.queue(0x1234, 32, 0);
  TEST_ASSERT_FALSE(sender.isJobPending());
}

void test_ir_sender_loop_transfers_job(void) {
  IRsend irsend(4);
  IrSender sender(irsend);

  sender.queue(0x1234, 32, 2);
  TEST_ASSERT_TRUE(sender.isJobPending());

  sender.loop();

  TEST_ASSERT_FALSE(sender.isJobPending());
  TEST_ASSERT_TRUE(sender.isActive());
}

// ---------------------------------------------------------------------------
// Unity setup
// ---------------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

void setup() {
  delay(2000);  // give serial monitor time to connect

  UNITY_BEGIN();

  // replayUrlFor
  RUN_TEST(test_replayUrlFor_nec_32bit);
  RUN_TEST(test_replayUrlFor_nec_16bit);
  RUN_TEST(test_replayUrlFor_non_nec_returns_empty);
  RUN_TEST(test_replayUrlFor_nec_case_insensitive);

  // saveUrlFor
  RUN_TEST(test_saveUrlFor_with_name);
  RUN_TEST(test_saveUrlFor_without_name);
  RUN_TEST(test_saveUrlFor_value_hex_format);

  // isHexValue
  RUN_TEST(test_isHexValue_valid);
  RUN_TEST(test_isHexValue_invalid);
  RUN_TEST(test_isHexValue_empty);
  RUN_TEST(test_isHexValue_null);

  // IrSender
  RUN_TEST(test_ir_sender_queue_basic);
  RUN_TEST(test_ir_sender_queue_invalid);
  RUN_TEST(test_ir_sender_loop_transfers_job);

  UNITY_END();
}

void loop() {}
