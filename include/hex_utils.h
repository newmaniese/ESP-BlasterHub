#ifndef HEX_UTILS_H
#define HEX_UTILS_H

#include <Arduino.h>

// Returns true if s is a non-empty string of hex digits.
// Note: Does not allow '0x' prefix.
bool isHexValue(const char* s);

// Converts a uint64_t to an 8-character zero-padded uppercase hex string.
// Truncates to 32 bits for compatibility with IR code representation.
String uint64ToHex(uint64_t val);

#endif // HEX_UTILS_H
