#include <Arduino.h>
#include <unity.h>
#include "ir_utils.h"

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
// sendUrlForSaved
// ---------------------------------------------------------------------------

void test_sendUrlForSaved_nec(void) {
  String url = sendUrlForSaved("NEC", "FF827D", 32);
  TEST_ASSERT_EQUAL_STRING("/send?type=nec&data=FF827D&length=32", url.c_str());
}

void test_sendUrlForSaved_nec_case_insensitive(void) {
  String url = sendUrlForSaved("nec", "ABCD", 16);
  TEST_ASSERT_TRUE(url.length() > 0);
  TEST_ASSERT_TRUE(url.indexOf("ABCD") >= 0);
}

void test_sendUrlForSaved_non_nec_returns_empty(void) {
  String url = sendUrlForSaved("Sony", "1234", 12);
  TEST_ASSERT_EQUAL_STRING("", url.c_str());
}

void test_sendUrlForSaved_null_protocol_returns_empty(void) {
  String url = sendUrlForSaved(nullptr, "FF", 32);
  TEST_ASSERT_EQUAL_STRING("", url.c_str());
}

void test_sendUrlForSaved_null_value_returns_empty(void) {
  String url = sendUrlForSaved("NEC", nullptr, 32);
  TEST_ASSERT_EQUAL_STRING("", url.c_str());
}

// ---------------------------------------------------------------------------
// escapeHtml
// ---------------------------------------------------------------------------

void test_escapeHtml_ampersand(void) {
  TEST_ASSERT_EQUAL_STRING("a&amp;b", escapeHtml("a&b").c_str());
}

void test_escapeHtml_less_than(void) {
  TEST_ASSERT_EQUAL_STRING("&lt;tag&gt;", escapeHtml("<tag>").c_str());
}

void test_escapeHtml_double_quote(void) {
  TEST_ASSERT_EQUAL_STRING("say &quot;hi&quot;", escapeHtml("say \"hi\"").c_str());
}

void test_escapeHtml_plain_string_unchanged(void) {
  TEST_ASSERT_EQUAL_STRING("hello world", escapeHtml("hello world").c_str());
}

void test_escapeHtml_empty_string(void) {
  TEST_ASSERT_EQUAL_STRING("", escapeHtml("").c_str());
}

void test_escapeHtml_all_special(void) {
  TEST_ASSERT_EQUAL_STRING("&amp;&lt;&gt;&quot;", escapeHtml("&<>\"").c_str());
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

  // sendUrlForSaved
  RUN_TEST(test_sendUrlForSaved_nec);
  RUN_TEST(test_sendUrlForSaved_nec_case_insensitive);
  RUN_TEST(test_sendUrlForSaved_non_nec_returns_empty);
  RUN_TEST(test_sendUrlForSaved_null_protocol_returns_empty);
  RUN_TEST(test_sendUrlForSaved_null_value_returns_empty);

  // escapeHtml
  RUN_TEST(test_escapeHtml_ampersand);
  RUN_TEST(test_escapeHtml_less_than);
  RUN_TEST(test_escapeHtml_double_quote);
  RUN_TEST(test_escapeHtml_plain_string_unchanged);
  RUN_TEST(test_escapeHtml_empty_string);
  RUN_TEST(test_escapeHtml_all_special);

  UNITY_END();
}

void loop() {}
