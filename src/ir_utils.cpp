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
