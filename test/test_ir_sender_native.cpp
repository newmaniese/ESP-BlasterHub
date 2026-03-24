#include <unity.h>
#include "IrSender.h"
#include "IRsend.h"
#include "Arduino.h"

unsigned long mock_millis = 0;

void test_IrSender_isActive_basic(void) {
    IRsend mockIr;
    IrSender sender(mockIr);

    // Initial state: not active
    TEST_ASSERT_FALSE(sender.isActive());

    // Queue a job
    sender.queue(0x12345678, 32, 2);
    // Still not active until loop() is called
    TEST_ASSERT_FALSE(sender.isActive());

    // First loop: becomes active and sends first repeat
    sender.loop();
    TEST_ASSERT_TRUE(sender.isActive());
    TEST_ASSERT_EQUAL(1, mockIr.sendCount);

    // Call loop again immediately (time not advanced)
    sender.loop();
    TEST_ASSERT_TRUE(sender.isActive()); // Should still be active, waiting for 50ms
    TEST_ASSERT_EQUAL(1, mockIr.sendCount); // No second send yet

    // Advance time by 60ms to allow next repeat
    mock_millis += 60;
    sender.loop();
    // After sending the last repeat (2nd), it immediately becomes inactive
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

    // Interrupt with a new command
    sender.queue(0xBBBB, 16, 1);
    sender.loop(); // loop() should pick up the new command immediately
    // Since repeats = 1, it should send once and then become inactive
    TEST_ASSERT_FALSE(sender.isActive());
    TEST_ASSERT_EQUAL(2, mockIr.sendCount);
    TEST_ASSERT_EQUAL(0xBBBB, mockIr.lastData);
}

void setUp(void) {
    mock_millis = 0;
}

void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_IrSender_isActive_basic);
    RUN_TEST(test_IrSender_interruption);
    return UNITY_END();
}
