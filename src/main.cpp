#include <Arduino.h>
#include <ctype.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "secrets.h"
#include "ir_utils.h"
#include "IrSender.h"
#include "ble_server.h"
#include "IrSender.h"

#define HISTORY_SIZE 5
#define SAVED_CODES_NAMESPACE "ir_saved"
#define SAVED_CODE_MAX 512   // NVS value limit ~508; keep JSON under this

const uint16_t RECV_PIN = 10;     // IR receiver on GPIO10 (ESP32-C3)
const uint16_t SEND_PIN = 4;      // IR LED on GPIO4

// IRremote settings
const uint16_t CAPTURE_BUF_SIZE = 1024;
const uint8_t RECV_TIMEOUT_MS = 50;    // 50ms, good for most remotes

IRrecv irrecv(RECV_PIN, CAPTURE_BUF_SIZE, RECV_TIMEOUT_MS, true);
IRsend irsend(SEND_PIN);
IrSender irSender(irsend);
decode_results results;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

String lastHumanReadable = "";
String lastRawJson = "";
uint32_t lastCodeSeq = 0;  // Incremented on each new IR decode; client polls /last to detect changes

IrCapture history[HISTORY_SIZE];
int historyLen = 0;

Preferences savedCodes;
// BLE callbacks and AsyncWebServer handlers run on different tasks, so NVS access
// through this shared Preferences instance must be serialized.
static SemaphoreHandle_t savedCodesMutex = nullptr;

static bool initSavedCodesMutex() {
  if (savedCodesMutex != nullptr) return true;
  savedCodesMutex = xSemaphoreCreateMutex();
  if (savedCodesMutex == nullptr) {
    printf("[IR] Failed to create saved codes mutex\n");
    return false;
  }
  return true;
}

class SavedCodesLock {
public:
  SavedCodesLock() : locked(false) {
    if (!initSavedCodesMutex()) return;
    locked = (xSemaphoreTake(savedCodesMutex, portMAX_DELAY) == pdTRUE);
    if (!locked) {
      printf("[IR] Failed to lock saved codes mutex\n");
    }
  }

  ~SavedCodesLock() {
    if (locked) xSemaphoreGive(savedCodesMutex);
  }

  explicit operator bool() const {
    return locked;
  }

private:
  bool locked;
};

int getSavedCount() {
  SavedCodesLock lock;
  if (!lock) return 0;
  savedCodes.begin(SAVED_CODES_NAMESPACE, true);
  int n = savedCodes.getInt("n", 0);
  savedCodes.end();
  return n;
}

// Build the JSON array of all saved codes (shared by HTTP and BLE).
String getSavedCodesJson() {
  SavedCodesLock lock;
  if (!lock) return "[]";
  savedCodes.begin(SAVED_CODES_NAMESPACE, true);
  int n = savedCodes.getInt("n", 0);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String key = String(i);
    String raw = savedCodes.getString(key.c_str(), "{}");
    JsonObject obj = arr.add<JsonObject>();
    JsonDocument entry;
    deserializeJson(entry, raw);
    obj["index"] = i;
    obj["name"] = entry["name"].as<const char *>();
    obj["protocol"] = entry["protocol"].as<const char *>();
    obj["value"] = entry["value"].as<const char *>();
    obj["bits"] = entry["bits"].as<uint16_t>();
  }
  savedCodes.end();
  String out;
  serializeJson(doc, out);
  return out;
}

// Compact JSON for BLE only (index + name, short keys) to stay under 600-byte characteristic limit.
// When truncated, a sentinel entry is appended so BLE clients can detect it and know total count.
static const size_t BLE_SAVED_CODES_MAX_LEN = 590;
// Reserve for truncation sentinel: ,{"i":-1,"n":"","_truncated":true,"_total":NNN} + ']'
static const size_t BLE_SAVED_TRUNCATED_SUFFIX_LEN = 50;
String getSavedCodesJsonCompact() {
  SavedCodesLock lock;
  if (!lock) return "[]";
  savedCodes.begin(SAVED_CODES_NAMESPACE, true);
  int n = savedCodes.getInt("n", 0);
  String out = "[";
  int i = 0;
  for (; i < n; i++) {
    String key = String(i);
    String raw = savedCodes.getString(key.c_str(), "{}");
    JsonDocument entry;
    deserializeJson(entry, raw);
    const char *name = entry["name"] | "";

    // Build this entry in a fragment so we can check length before appending.
    String frag;
    if (out.length() > 1) frag += ",";
    frag += "{\"i\":";
    frag += String(i);
    frag += ",\"n\":\"";
    for (const char *p = name; *p; p++) {
      unsigned char c = (unsigned char)*p;
      if (c == '"' || c == '\\') {
        frag += '\\';
        frag += (char)c;
      } else if (c == '\b') {
        frag += "\\b";
      } else if (c == '\t') {
        frag += "\\t";
      } else if (c == '\n') {
        frag += "\\n";
      } else if (c == '\f') {
        frag += "\\f";
      } else if (c == '\r') {
        frag += "\\r";
      } else if (c < 0x20) {
        frag += "\\u";
        char hex[5];
        snprintf(hex, sizeof(hex), "%04x", c);
        frag += hex;
      } else {
        frag += (char)c;
      }
    }
    frag += "\"}";

    if (out.length() + frag.length() + BLE_SAVED_TRUNCATED_SUFFIX_LEN > BLE_SAVED_CODES_MAX_LEN)
      break;
    out += frag;
  }
  if (i < n) {
    if (out.length() > 1) out += ",";
    out += "{\"i\":-1,\"n\":\"\",\"_truncated\":true,\"_total\":";
    out += String(n);
    out += "}";
  }
  out += "]";
  savedCodes.end();
  return out;
}

// Find first saved code index whose name matches (case-insensitive). Returns -1 if not found.
int getSavedCodeIndexByName(const char *name) {
  if (!name || !*name) return -1;
  SavedCodesLock lock;
  if (!lock) return -1;
  savedCodes.begin(SAVED_CODES_NAMESPACE, true);
  int n = savedCodes.getInt("n", 0);
  for (int i = 0; i < n; i++) {
    String key = String(i);
    String raw = savedCodes.getString(key.c_str(), "{}");
    JsonDocument entry;
    if (deserializeJson(entry, raw)) continue;
    const char *stored = entry["name"] | "";
    if (strcasecmp(stored, name) == 0) {
      savedCodes.end();
      return i;
    }
  }
  savedCodes.end();
  return -1;
}

// Send a stored IR code by NVS index.  Shared by HTTP, WebSocket, and BLE.
// Returns true on success; fills outName with the code's stored name.
bool sendSavedCode(int index, String &outName) {
  String raw;
  {
    SavedCodesLock lock;
    if (!lock) {
      outName = "";
      return false;
    }
    savedCodes.begin(SAVED_CODES_NAMESPACE, true);
    int n = savedCodes.getInt("n", 0);
    if (index < 0 || index >= n) {
      savedCodes.end();
      outName = "";
      return false;
    }
    String key = String(index);
    raw = savedCodes.getString(key.c_str(), "{}");
    savedCodes.end();
  }

  JsonDocument entry;
  DeserializationError err = deserializeJson(entry, raw);
  if (err) {
    outName = "";
    return false;
  }

  outName = entry["name"] | "";
  const char *protocol = entry["protocol"] | "";
  const char *valueHex = entry["value"] | "";
  uint16_t bits = entry["bits"] | 32;

  if (String(protocol).equalsIgnoreCase("NEC") && strlen(valueHex) > 0) {
    uint32_t value = strtoul(valueHex, nullptr, 16);
    irSender.queue(value, bits, 1);
    printf("[IR] TX NEC 0x%s %db (%s)\n", valueHex, bits, outName.length() ? outName.c_str() : "no name");
    return true;
  }

  printf("[IR] Unsupported protocol for saved code #%d: %s\n", index, protocol);
  return false;
}

// Template processor for LittleFS pages — replaces %PLACEHOLDER% tokens.
String templateProcessor(const String& var) {
  if (var == "DEVICE_IP") return WiFi.localIP().toString();
  if (var == "INITIAL_SAVED_COUNT") return String(getSavedCount());
  return String();
}

// POST /save — body JSON: { "name": "Power", "protocol": "NEC", "value": "FF827D", "bits": 32 }
// Body handler accumulates and processes when complete.
void onSaveBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  const size_t MAX_BODY_SIZE = 2048;
  if (total > MAX_BODY_SIZE) {
    if (index == 0) request->send(413, "application/json", "{\"error\":\"Payload too large\"}");
    return;
  }
  String *acc = (String *)request->_tempObject;
  if (acc == nullptr) {
    acc = new String();
    request->_tempObject = acc;
  }
  if (len) acc->concat((const char *)data, len);
  if (index + len != total) return;

  String body = *acc;
  delete acc;
  request->_tempObject = nullptr;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  const char *name = doc["name"] | "";
  const char *protocol = doc["protocol"] | "UNKNOWN";
  const char *valueHex = doc["value"];
  uint16_t bits = doc["bits"] | 32;
  if (!valueHex) {
    request->send(400, "application/json", "{\"error\":\"Missing value\"}");
    return;
  }
  SavedCodesLock lock;
  if (!lock) {
    request->send(500, "application/json", "{\"error\":\"Storage unavailable\"}");
    return;
  }
  savedCodes.begin(SAVED_CODES_NAMESPACE, false);
  int n = savedCodes.getInt("n", 0);
  JsonObject obj = doc.to<JsonObject>();
  obj["name"] = name;
  obj["protocol"] = protocol;
  obj["value"] = valueHex;
  obj["bits"] = bits;
  char buf[SAVED_CODE_MAX];
  size_t outLen = serializeJson(doc, buf, sizeof(buf));
  if (outLen >= SAVED_CODE_MAX) {
    savedCodes.end();
    request->send(413, "application/json", "{\"error\":\"Code too large\"}");
    return;
  }
  String key = String(n);
  savedCodes.putString(key.c_str(), buf);
  savedCodes.putInt("n", n + 1);
  savedCodes.end();
  request->send(200, "application/json", "{\"ok\":true,\"index\":" + String(n) + ",\"total\":" + String(n + 1) + "}");
}

// POST /saved/import — body JSON array of { "name", "protocol", "value", "bits" }.
// Appends valid entries to NVS and skips invalid entries with a summary.
void onSavedImportBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  const size_t MAX_IMPORT_SIZE = 10240;
  if (total > MAX_IMPORT_SIZE) {
    if (index == 0) request->send(413, "application/json", "{\"ok\":false,\"error\":\"Payload too large\"}");
    return;
  }
  String *acc = (String *)request->_tempObject;
  if (acc == nullptr) {
    acc = new String();
    request->_tempObject = acc;
  }
  if (len) acc->concat((const char *)data, len);
  if (index + len != total) return;
  String body = *acc;
  delete acc;
  request->_tempObject = nullptr;

  JsonDocument inputDoc;
  DeserializationError err = deserializeJson(inputDoc, body);
  if (err) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  if (!inputDoc.is<JsonArray>()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Expected JSON array\"}");
    return;
  }

  JsonArray in = inputDoc.as<JsonArray>();
  JsonDocument outDoc;
  outDoc["ok"] = true;
  outDoc["imported"] = 0;
  outDoc["skipped"] = 0;
  JsonArray errors = outDoc["errors"].to<JsonArray>();

  SavedCodesLock lock;
  if (!lock) {
    request->send(500, "application/json", "{\"ok\":false,\"error\":\"Storage unavailable\"}");
    return;
  }
  savedCodes.begin(SAVED_CODES_NAMESPACE, false);
  int n = savedCodes.getInt("n", 0);

  const int maxErrors = 12;
  for (size_t i = 0; i < in.size(); i++) {
    JsonVariant v = in[i];
    if (!v.is<JsonObject>()) {
      outDoc["skipped"] = (int)outDoc["skipped"] + 1;
      if ((int)errors.size() < maxErrors) {
        JsonObject e = errors.add<JsonObject>();
        e["index"] = (int)i;
        e["reason"] = "Entry is not an object";
      }
      continue;
    }

    JsonObject src = v.as<JsonObject>();
    const char *name = src["name"] | "";
    const char *protocol = src["protocol"] | "";
    const char *valueHex = src["value"] | "";
    uint16_t bits = src["bits"] | 32;

    const char *reason = nullptr;
    if (!protocol || !*protocol) reason = "Missing protocol";
    else if (!valueHex || !*valueHex) reason = "Missing value";
    else if (!isHexValue(valueHex)) reason = "Value must be hex";
    else if (bits < 1 || bits > 64) reason = "Bits out of range";

    if (reason) {
      outDoc["skipped"] = (int)outDoc["skipped"] + 1;
      if ((int)errors.size() < maxErrors) {
        JsonObject e = errors.add<JsonObject>();
        e["index"] = (int)i;
        e["reason"] = reason;
      }
      continue;
    }

    JsonDocument entry;
    entry["name"] = name;
    entry["protocol"] = protocol;
    entry["value"] = valueHex;
    entry["bits"] = bits;

    char buf[SAVED_CODE_MAX];
    size_t outLen = serializeJson(entry, buf, sizeof(buf));
    if (outLen >= SAVED_CODE_MAX) {
      outDoc["skipped"] = (int)outDoc["skipped"] + 1;
      if ((int)errors.size() < maxErrors) {
        JsonObject e = errors.add<JsonObject>();
        e["index"] = (int)i;
        e["reason"] = "Entry too large";
      }
      continue;
    }

    String key = String(n);
    savedCodes.putString(key.c_str(), buf);
    n++;
    outDoc["imported"] = (int)outDoc["imported"] + 1;
  }

  savedCodes.putInt("n", n);
  savedCodes.end();
  outDoc["total"] = n;

  String out;
  serializeJson(outDoc, out);
  request->send(200, "application/json", out);
}

// GET /save or POST with query params: save last code or specific code via query params
void handleSaveGet(AsyncWebServerRequest *request) {
  String name = request->hasParam("name") ? request->getParam("name")->value() : "";
  String protocol, valueHex;
  uint16_t bits = 32;
  if (request->hasParam("protocol") && request->hasParam("value")) {
    protocol = request->getParam("protocol")->value();
    valueHex = request->getParam("value")->value();
    if (request->hasParam("length")) bits = (uint16_t)request->getParam("length")->value().toInt();
  } else {
    if (historyLen == 0) {
      request->send(400, "text/plain", "No code to save; receive an IR code first.");
      return;
    }
    const IrCapture &c = history[0];
    protocol = c.protocol;
    char buf[20];
    sprintf(buf, "%08lX", (unsigned long)(c.value & 0xFFFFFFFF));
    valueHex = buf;
    bits = c.bits;
  }
  SavedCodesLock lock;
  if (!lock) {
    request->send(500, "application/json", "{\"error\":\"Storage unavailable\"}");
    return;
  }
  savedCodes.begin(SAVED_CODES_NAMESPACE, false);
  int n = savedCodes.getInt("n", 0);
  JsonDocument doc;
  doc["name"] = name;
  doc["protocol"] = protocol;
  doc["value"] = valueHex;
  doc["bits"] = bits;
  char buf[SAVED_CODE_MAX];
  size_t outLen = serializeJson(doc, buf, sizeof(buf));
  String key = String(n);
  savedCodes.putString(key.c_str(), buf);
  savedCodes.putInt("n", n + 1);
  savedCodes.end();
  request->send(200, "application/json", "{\"ok\":true,\"index\":" + String(n) + ",\"total\":" + String(n + 1) + "}");
}

// GET /saved — JSON array of saved codes
void handleSaved(AsyncWebServerRequest *request) {
  String out = getSavedCodesJson();
  request->send(200, "application/json", out);
}

// POST /saved/delete?index=N — remove saved code at index; shift rest down
void handleSavedDelete(AsyncWebServerRequest *request) {
  if (!request->hasParam("index")) {
    request->send(400, "application/json", "{\"error\":\"Missing index\"}");
    return;
  }
  int index = request->getParam("index")->value().toInt();
  SavedCodesLock lock;
  if (!lock) {
    request->send(500, "application/json", "{\"error\":\"Storage unavailable\"}");
    return;
  }
  savedCodes.begin(SAVED_CODES_NAMESPACE, false);
  int n = savedCodes.getInt("n", 0);
  if (index < 0 || index >= n) {
    savedCodes.end();
    request->send(400, "application/json", "{\"error\":\"Invalid index\"}");
    return;
  }
  for (int i = index; i < n - 1; i++) {
    String nextRaw = savedCodes.getString(String(i + 1).c_str(), "{}");
    savedCodes.putString(String(i).c_str(), nextRaw);
  }
  savedCodes.putInt("n", n - 1);
  savedCodes.end();
  request->send(200, "application/json", "{\"ok\":true,\"remaining\":" + String(n - 1) + "}");
}

// POST /saved/rename?index=N&name=NewName — rename one saved code.
void handleSavedRename(AsyncWebServerRequest *request) {
  if (!request->hasParam("index") || !request->hasParam("name")) {
    request->send(400, "application/json", "{\"error\":\"Missing index or name\"}");
    return;
  }
  int index = request->getParam("index")->value().toInt();
  String newName = request->getParam("name")->value();
  SavedCodesLock lock;
  if (!lock) {
    request->send(500, "application/json", "{\"error\":\"Storage unavailable\"}");
    return;
  }
  savedCodes.begin(SAVED_CODES_NAMESPACE, false);
  int n = savedCodes.getInt("n", 0);
  if (index < 0 || index >= n) {
    savedCodes.end();
    request->send(400, "application/json", "{\"error\":\"Invalid index\"}");
    return;
  }
  String key = String(index);
  String raw = savedCodes.getString(key.c_str(), "{}");
  JsonDocument entry;
  DeserializationError err = deserializeJson(entry, raw);
  if (err) {
    savedCodes.end();
    request->send(500, "application/json", "{\"error\":\"Stored code parse failed\"}");
    return;
  }
  entry["name"] = newName;
  char buf[SAVED_CODE_MAX];
  size_t len = serializeJson(entry, buf, sizeof(buf));
  if (len >= SAVED_CODE_MAX) {
    savedCodes.end();
    request->send(413, "application/json", "{\"error\":\"Name too long\"}");
    return;
  }
  savedCodes.putString(key.c_str(), buf);
  savedCodes.end();
  request->send(200, "application/json", "{\"ok\":true,\"index\":" + String(index) + "}");
}

// GET /dump — plain text for hardcoding (C-style)
void handleDump(AsyncWebServerRequest *request) {
  SavedCodesLock lock;
  if (!lock) {
    request->send(500, "text/plain", "Storage unavailable");
    return;
  }
  savedCodes.begin(SAVED_CODES_NAMESPACE, true);
  int n = savedCodes.getInt("n", 0);
  String out = "// Saved IR codes — paste into firmware\n";
  out += "// Count: " + String(n) + "\n\n";
  for (int i = 0; i < n; i++) {
    String key = String(i);
    String raw = savedCodes.getString(key.c_str(), "{}");
    JsonDocument entry;
    deserializeJson(entry, raw);
    const char *name = entry["name"] | "";
    String protocol = entry["protocol"] | "UNKNOWN";
    const char *valueHex = entry["value"] | "0";
    uint16_t bits = entry["bits"] | 32;
    out += "// " + String(i) + " " + String(name) + " " + protocol + " 0x" + String(valueHex) + " " + String(bits) + "b\n";
    if (protocol.equalsIgnoreCase("NEC"))
      out += "irsend.sendNEC(0x" + String(valueHex) + "u, " + String(bits) + ");  // " + String(name) + "\n";
    else
      out += "// irsend.send... (unsupported protocol); value=0x" + String(valueHex) + " " + String(name) + "\n";
  }
  savedCodes.end();
  request->send(200, "text/plain", out);
}

void handleRoot(AsyncWebServerRequest *request) {
  printf("[IR] Root page requested\n");
  request->send(LittleFS, "/index.html", "text/html", false, templateProcessor);
}

// GET /last — JSON for live-update polling: { seq, human, raw, replayUrl }
void handleLast(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["seq"] = lastCodeSeq;
  doc["human"] = lastHumanReadable;
  doc["raw"] = lastRawJson;
  String replayUrl = (historyLen > 0) ? replayUrlFor(history[0]) : "";
  doc["replayUrl"] = replayUrl;
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

// Simple NEC-style sender: /send?type=nec&data=FF827D&length=32
void handleSend(AsyncWebServerRequest *request) {
  if (!request->hasParam("type") || !request->hasParam("data")) {
    request->send(400, "text/plain", "Missing type or data");
    return;
  }

  String type = request->getParam("type")->value();
  String data = request->getParam("data")->value();
  int length = request->hasParam("length") ? request->getParam("length")->value().toInt() : 32;
  int repeat = request->hasParam("repeat") ? request->getParam("repeat")->value().toInt() : 1;

  if (length < 1 || length > 128) {
    request->send(400, "text/plain", "Invalid length (1-128)");
    return;
  }
  if (repeat < 1 || repeat > 20) {
    request->send(400, "text/plain", "Invalid repeat (1-20)");
    return;
  }

  if (type == "nec") {
    if (!isHexValue(data.c_str())) {
      request->send(400, "text/plain", "Invalid hex data");
      return;
    }
    uint32_t value = strtoul(data.c_str(), nullptr, 16);
    irSender.queue(value, length, repeat);
    printf("[IR] TX NEC 0x%s %db (no name)\n", data.c_str(), length);
    request->send(200, "text/plain", "Sent NEC " + data);
  } else {
    request->send(400, "text/plain", "Unsupported type");
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    // Send current last code so new client gets state
    JsonDocument doc;
    doc["event"] = "ir";
    doc["seq"] = lastCodeSeq;
    doc["human"] = lastHumanReadable;
    doc["raw"] = lastRawJson;
    doc["replayUrl"] = (historyLen > 0) ? replayUrlFor(history[0]) : "";
    if (historyLen > 0) {
      doc["protocol"] = history[0].protocol;
      char valHex[20];
      sprintf(valHex, "%08lX", (unsigned long)(history[0].value & 0xFFFFFFFF));
      doc["value"] = valHex;
      doc["bits"] = history[0].bits;
    }
    String out;
    serializeJson(doc, out);
    client->text(out);
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->opcode != WS_TEXT) return;
    if (len == 0) return;
    JsonDocument req;
    DeserializationError err = deserializeJson(req, data, len);
    if (err) return;
    if (!req["cmd"].is<const char *>() || String(req["cmd"].as<const char *>()) != "send") return;
    String stype = req["type"] | "";
    String sdata = req["data"] | "";
    int length = req["length"] | 32;
    String name = req["name"] | "";
    if (stype == "nec" && sdata.length() > 0) {
      if (!isHexValue(sdata.c_str()) || length < 1 || length > 128) {
        JsonDocument nack;
        nack["ok"] = false;
        nack["error"] = "Invalid hex data or length";
        String nackStr;
        serializeJson(nack, nackStr);
        client->text(nackStr);
        return;
      }
      uint32_t value = strtoul(sdata.c_str(), nullptr, 16);
      irSender.queue(value, length, 1);
      printf("[IR] TX NEC 0x%s %db (%s)\n", sdata.c_str(), length, name.length() ? name.c_str() : "no name");
      JsonDocument ack;
      ack["ok"] = true;
      ack["msg"] = "Sent NEC " + sdata;
      if (name.length() > 0) ack["name"] = name;
      String ackStr;
      serializeJson(ack, ackStr);
      client->text(ackStr);
    }
  }
}

void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  printf("[IR] Connecting to Wi-Fi\n");
  const uint32_t timeoutMs = 20000;  // 20 s
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= timeoutMs) {
      printf("[IR] WiFi timeout – check SSID/password. Continuing without network.\n");
      return;
    }
    delay(500);
    printf(".");
  }
  printf("\n[IR] IP: %s", WiFi.localIP().toString().c_str());
  uint32_t sec;
  char cmd[BLE_SCHEDULE_CMD_NAME_MAX];
  if (getScheduleCountdown(&sec, cmd, sizeof(cmd))) {
    printf("  (%u s until %s)", (unsigned)sec, cmd);
  }
  printf("\n");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  printf("[IR] --- ESP32-C3 IR Blaster boot ---\n");
  if (!initSavedCodesMutex()) {
    printf("[IR] WARNING: saved codes mutex unavailable; storage operations may fail\n");
  }

  setupWifi();

  if (!LittleFS.begin(true)) {
    printf("[IR] LittleFS mount failed!\n");
  } else {
    printf("[IR] LittleFS mounted\n");
  }

  irrecv.enableIRIn();
  irsend.begin();

  server.on("/", HTTP_GET, handleRoot);
  // Serve static assets from LittleFS (app.css, app.js, etc.)
  server.serveStatic("/app.css", LittleFS, "/app.css").setCacheControl("max-age=86400");
  server.serveStatic("/app.js", LittleFS, "/app.js").setCacheControl("max-age=86400");
  server.on("/ip", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", WiFi.localIP().toString()); });
  server.on("/last", HTTP_GET, handleLast);
  server.on("/send", HTTP_GET, handleSend);
  server.on("/save", HTTP_GET, handleSaveGet);
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) { /* body handled in onSaveBody */ }, nullptr, onSaveBody);
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/saved", HTTP_GET, handleSaved);
  server.on("/saved/import", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->contentLength() == 0) {
      request->send(411, "application/json", "{\"ok\":false,\"error\":\"Content-Length required\"}");
      return;
    }
    /* body handled in onSavedImportBody */
  }, nullptr, onSavedImportBody);
  server.on("/saved/delete", HTTP_POST, handleSavedDelete);
  server.on("/saved/rename", HTTP_POST, handleSavedRename);
  server.on("/dump", HTTP_GET, handleDump);
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(204, "text/plain", ""); });
  server.onNotFound([](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });
  server.begin();

  printf("[IR] HTTP IR server started\n");

  setupBLE();
}

void loop() {
  irSender.loop();

  // Heartbeat every 1s so you always see output after opening the monitor
  static uint32_t lastStatusPrint = 0;
  if (millis() - lastStatusPrint >= 1000) {
    lastStatusPrint = millis();
    if (WiFi.status() == WL_CONNECTED) {
      printf("[IR] IP: %s", WiFi.localIP().toString().c_str());
      uint32_t sec;
      char cmd[BLE_SCHEDULE_CMD_NAME_MAX];
      if (getScheduleCountdown(&sec, cmd, sizeof(cmd))) {
        printf("  (%u s until %s)", (unsigned)sec, cmd);
      }
      printf("\n");
    } else {
      printf("[IR] (WiFi not connected)\n");
    }
  }

  // IR receive loop
  if (irrecv.decode(&results)) {
    lastHumanReadable = resultToHumanReadableBasic(&results);
    lastRawJson = resultToSourceCode(&results);
    lastCodeSeq++;

    // Add to history (newest first)
    for (int i = HISTORY_SIZE - 1; i > 0; i--) history[i] = history[i - 1];
    history[0].protocol = typeToString(results.decode_type);
    history[0].value = results.value;
    history[0].bits = results.bits;
    history[0].human = lastHumanReadable;
    if (historyLen < HISTORY_SIZE) historyLen++;

    printf("[IR] %s\n", lastHumanReadable.c_str());
    printf("[IR] %s\n", lastRawJson.c_str());

    if (ws.count() > 0) {
      JsonDocument doc;
      doc["event"] = "ir";
      doc["seq"] = lastCodeSeq;
      doc["human"] = lastHumanReadable;
      doc["raw"] = lastRawJson;
      doc["replayUrl"] = replayUrlFor(history[0]);
      doc["protocol"] = history[0].protocol;
      char valHex[20];
      sprintf(valHex, "%08lX", (unsigned long)(history[0].value & 0xFFFFFFFF));
      doc["value"] = valHex;
      doc["bits"] = history[0].bits;
      String out;
      serializeJson(doc, out);
      ws.textAll(out);
    }

    irrecv.resume();
  }

  loopBLE();

  // AsyncWebServer handles HTTP in background.
}
