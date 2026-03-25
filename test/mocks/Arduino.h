#ifndef ARDUINO_H
#define ARDUINO_H

#include <cstdint>
#include <iostream>
#include <stdint.h>
#include <string>

extern unsigned long mock_millis;
inline unsigned long millis() { return mock_millis; }

class String {
public:
  String() {}
  String(const char *s) : str(s ? s : "") {}
  String(const std::string &s) : str(s) {}

  const char *c_str() const { return str.c_str(); }
  size_t length() const { return str.length(); }
  void reserve(size_t n) { str.reserve(n); }
  void concat(const char *s) { str += s; }
  void concat(const String &s) { str += s.str; }
  void toUpperCase() {
    for (char &c : str) {
      if (c >= 'a' && c <= 'z') c -= 32;
    }
  }

  bool operator==(const char *s) const { return str == s; }
  bool operator==(const String &s) const { return str == s.str; }
  bool operator!=(const char *s) const { return str != s; }
  bool operator!=(const String &s) const { return str != s.str; }
  String operator+(const char *s) const { return String(str + s); }
  String operator+(const String &s) const { return String(str + s.str); }

private:
  std::string str;
};

#endif
