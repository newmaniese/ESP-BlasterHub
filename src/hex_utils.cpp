#include "hex_utils.h"
#include <ctype.h>
#include <stdio.h>

bool isHexValue(const char *s) {
  if (!s || !*s) return false;
  for (const char *p = s; *p; ++p) {
    if (!isxdigit((unsigned char)*p)) return false;
  }
  return true;
}

String uint64ToHex(uint64_t val) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%08lX", (unsigned long)(val & 0xFFFFFFFF));
  return String(buf);
}
