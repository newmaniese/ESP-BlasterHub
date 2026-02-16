#ifndef IR_UTILS_H
#define IR_UTILS_H

#include <Arduino.h>

struct IrCapture {
  String protocol;
  uint64_t value;
  uint16_t bits;
  String human;
};

// Build replay URL for protocols we can send (currently NEC only). Returns empty if not supported.
String replayUrlFor(const IrCapture& c);

// Build /save URL for a capture, with optional name query param.
String saveUrlFor(const IrCapture& c, const String& name);

// Build /send URL for a saved code (NEC only). Returns empty if not sendable.
String sendUrlForSaved(const char* protocol, const char* valueHex, uint16_t bits);

// Escape &, <, >, " for safe HTML embedding.
String escapeHtml(const String& s);

// Returns true if s is a non-empty string of hex digits.
bool isHexValue(const char* s);

#endif // IR_UTILS_H
