#ifndef IRSEND_MOCK_H
#define IRSEND_MOCK_H

#include <stdint.h>

class IRsend {
public:
    void begin() {}
    void sendNEC(uint32_t data, uint16_t nbits) {
        lastData = data;
        lastNBits = nbits;
        sendCount++;
    }
    uint32_t lastData = 0;
    uint16_t lastNBits = 0;
    int sendCount = 0;
};

#endif
