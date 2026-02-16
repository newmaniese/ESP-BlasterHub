#ifndef IR_SENDER_H
#define IR_SENDER_H

#include <Arduino.h>
#include <IRsend.h>
#include <mutex>

class IrSender {
public:
    // Pass the global IRsend object by reference
    IrSender(IRsend& irsend);

    // Queue an IR send command (thread-safe, non-blocking)
    // Overwrites any pending command. If a command is currently sending,
    // the new command will start as soon as possible (next loop iteration),
    // interrupting the current sequence.
    void queue(uint32_t value, uint16_t length, int repeat);

    // Call this in the main loop to process the queue
    void loop();

    // Check if currently busy sending
    bool isActive() const;

private:
    IRsend& _irsend;

    mutable std::mutex _mutex;

    // Shared state (protected by mutex)
    uint32_t _pendingValue;
    uint16_t _pendingLength;
    int _pendingRepeats;
    bool _jobPending;

    // Internal state (only accessed by loop)
    uint32_t _currentValue;
    uint16_t _currentLength;
    int _currentRepeatsLeft;
    unsigned long _lastSendTime;
    bool _active;
    bool _startImmediate;
};

#endif
