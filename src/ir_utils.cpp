#include "ir_utils.h"
#include <ctype.h>

bool isHexValue(const char *s) {
  if (!s || !*s) return false;
  for (const char *p = s; *p; ++p) {
    if (!isxdigit((unsigned char)*p)) return false;
  }
  return true;
}

String replayUrlFor(const IrCapture& c) {
  if (!c.protocol.equalsIgnoreCase("NEC")) return "";
  char buf[16];
  sprintf(buf, "%08lX", (unsigned long)(c.value & 0xFFFFFFFF));
  return "/send?type=nec&data=" + String(buf) + "&length=" + String(c.bits);
}

String saveUrlFor(const IrCapture& c, const String& name) {
  char valueHex[20];
  sprintf(valueHex, "%08lX", (unsigned long)(c.value & 0xFFFFFFFF));
  String url = "/save?protocol=" + c.protocol + "&value=" + String(valueHex) + "&length=" + String(c.bits);
  if (name.length() > 0) url += "&name=" + name;
  return url;
}

String sendUrlForSaved(const char* protocol, const char* valueHex, uint16_t bits) {
  if (protocol == nullptr || valueHex == nullptr) return "";
  if (String(protocol).equalsIgnoreCase("NEC")) {
    return "/send?type=nec&data=" + String(valueHex) + "&length=" + String(bits);
  }
  return "";
}

String escapeHtml(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}
