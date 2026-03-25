#include "ir_utils.h"
#include <ctype.h>

String replayUrlFor(const IrCapture& c) {
  if (!c.protocol.equalsIgnoreCase("NEC")) return "";
  return "/send?type=nec&data=" + uint64ToHex(c.value) + "&length=" + String(c.bits);
}

String saveUrlFor(const IrCapture& c, const String& name) {
  String url = "/save?protocol=" + c.protocol + "&value=" + uint64ToHex(c.value) + "&length=" + String(c.bits);
  if (name.length() > 0) url += "&name=" + name;
  return url;
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
