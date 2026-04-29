#include "hex_utils.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

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

bool parseHex32(const char* s, uint32_t& out_value) {
  if (!isHexValue(s)) {
    return false;
  }
  char* endptr;
  errno = 0;
  unsigned long long val = strtoull(s, &endptr, 16);
  if (errno == ERANGE) {
    return false;
  }
  if (*endptr != '\0') {
    return false;
  }
  if (val > 0xFFFFFFFF) {
    return false;
  }
  out_value = (uint32_t)val;
  return true;
}
