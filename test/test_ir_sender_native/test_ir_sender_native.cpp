#include <unity.h>
#include "Arduino.h"
#include "IrSender.h"
#include "IRsend.h"

void setUp(void) {
    mock_millis = 0;
}

void tearDown(void) {
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

void test_IrSender_queue_invalid_repeat(void) {
    IRsend mockIr;
    IrSender sender(mockIr);

    sender.queue(0x1234, 16, 0); // Invalid repeat (must be >= 1)
    TEST_ASSERT_FALSE(sender.isJobPending());

    sender.loop();
    TEST_ASSERT_FALSE(sender.isActive());
    TEST_ASSERT_EQUAL(0, mockIr.sendCount);
}

void test_IrSender_isJobPending(void) {
    IRsend mockIr;
    IrSender sender(mockIr);

    TEST_ASSERT_FALSE(sender.isJobPending());

    sender.queue(0x1234, 16, 2);
    TEST_ASSERT_TRUE(sender.isJobPending());

    sender.loop();
    TEST_ASSERT_FALSE(sender.isJobPending());
    TEST_ASSERT_TRUE(sender.isActive());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_IrSender_isActive_basic);
    RUN_TEST(test_IrSender_interruption);
    RUN_TEST(test_IrSender_queue_invalid_repeat);
    RUN_TEST(test_IrSender_isJobPending);
    return UNITY_END();
}
