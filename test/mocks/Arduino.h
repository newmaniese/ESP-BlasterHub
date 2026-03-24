#ifndef ARDUINO_MOCK_H
#define ARDUINO_MOCK_H

#include <stdint.h>

extern unsigned long mock_millis;
inline unsigned long millis() { return mock_millis; }

#endif
