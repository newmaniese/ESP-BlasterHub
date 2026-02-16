#include "hex_utils.h"
#include <ctype.h>

bool isHexValue(const char *s) {
  if (!s || !*s) return false;
  for (const char *p = s; *p; ++p) {
    if (!isxdigit((unsigned char)*p)) return false;
  }
  return true;
}
