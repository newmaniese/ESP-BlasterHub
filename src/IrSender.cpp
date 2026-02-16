#include "IrSender.h"

IrSender::IrSender(IRsend& irsend)
    : _irsend(irsend), _mutex(),
      _pendingValue(0), _pendingLength(0), _pendingRepeats(0), _jobPending(false),
      _currentValue(0), _currentLength(0), _currentRepeatsLeft(0),
      _lastSendTime(0), _active(false), _startImmediate(false) {}

void IrSender::queue(uint32_t value, uint16_t length, int repeat) {
    if (repeat < 1) return;

    std::lock_guard<std::mutex> lock(_mutex);
    _pendingValue = value;
    _pendingLength = length;
    _pendingRepeats = repeat;
    _jobPending = true;
}

void IrSender::loop() {
    // Check if there is a new job
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_jobPending) {
            _currentValue = _pendingValue;
            _currentLength = _pendingLength;
            _currentRepeatsLeft = _pendingRepeats;
            _jobPending = false;

            _active = true;
            _startImmediate = true;
        }
    }

    if (!_active) return;

    unsigned long now = millis();

    // Check if we can send now: either it's the first time (_startImmediate)
    // or enough time (50ms) has passed since the last send.
    if (_startImmediate || (now - _lastSendTime >= 50)) {
        if (_currentRepeatsLeft > 0) {
            _irsend.sendNEC(_currentValue, _currentLength);
            _lastSendTime = millis();
            _startImmediate = false;
            _currentRepeatsLeft--;
        }

        if (_currentRepeatsLeft <= 0) {
            _active = false;
        }
    }
}

bool IrSender::isActive() const {
    return _active;
}
