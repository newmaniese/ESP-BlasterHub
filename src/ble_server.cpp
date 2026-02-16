// BLE GATT server for the IR Blaster.
//
// Uses the Arduino-ESP32 built-in BLE library (Bluedroid stack).
//
// Exposes four characteristics behind bonded encryption:
//   - Saved Codes  (Read)   — JSON array of stored IR commands
//   - Send Command (Write)  — write a single byte (NVS index) to send that code
//   - Status       (Notify) — result string after a send ("OK:<name>" or "ERR:…")
//   - Schedule     (Write)  — JSON: arm delayed command or accept heartbeat keepalive
//
// Security: bonding + MITM + Secure Connections, passkey displayed on Serial.
// Auto-reconnect: advertising restarts on disconnect so the client reconnects.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "ble_server.h"

// ---------------------------------------------------------------------------
// External helpers defined in main.cpp
// ---------------------------------------------------------------------------
extern String getSavedCodesJson();
extern String getSavedCodesJsonCompact();
extern int    getSavedCodeIndexByName(const char *name);
extern bool   sendSavedCode(int index, String &outName);

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static BLEServer*         pServer       = nullptr;
static BLECharacteristic* pSavedChar   = nullptr;
static BLECharacteristic* pSendChar    = nullptr;
static BLECharacteristic* pStatusChar  = nullptr;
static BLECharacteristic* pScheduleChar = nullptr;
static bool               deviceConnected = false;

// Delayed command: run a saved code by name after delay_seconds once client disconnects.
// Countdown starts only when client disconnects (countdownStartMs set in onDisconnect).
static char     scheduledCommandName[BLE_SCHEDULE_CMD_NAME_MAX] = "";
static uint32_t scheduledDelayMs   = 0;
static unsigned long countdownStartMs = 0; // when client disconnected — countdown runs from here
static bool     scheduledArmed    = false;

// Helper: set Status characteristic and notify if connected.
static void setStatus(const String& msg) {
  pStatusChar->setValue(msg.c_str());
  if (deviceConnected) {
    pStatusChar->notify();
  }
}

// ---------------------------------------------------------------------------
// Server callbacks — connect / disconnect
// ---------------------------------------------------------------------------
class IRServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    (void)pServer;
    deviceConnected = true;
    printf("[BLE] Client connected\n");
  }

  void onDisconnect(BLEServer* pServer) override {
    (void)pServer;
    deviceConnected = false;
    // Start countdown only when client disconnects (N seconds until scheduled command).
    if (scheduledArmed) {
      countdownStartMs = millis();
    }
    printf("[BLE] Client disconnected — restarting advertising\n");
    BLEDevice::startAdvertising();
  }
};

// ---------------------------------------------------------------------------
// Security callbacks — passkey (when BLE_USE_PASSKEY) or Just Works
// ---------------------------------------------------------------------------
class IRSecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override {
#if BLE_USE_PASSKEY
    printf("[BLE] *** Pairing passkey: %06u — enter this on the client ***\n", (unsigned)BLE_PASSKEY);
    return BLE_PASSKEY;
#else
    return 0;
#endif
  }

  void onPassKeyNotify(uint32_t pass_key) override {
#if BLE_USE_PASSKEY
    printf("[BLE] *** Pairing passkey (display): %06u ***\n", (unsigned)pass_key);
#else
    (void)pass_key;
#endif
  }

  bool onSecurityRequest() override {
    printf("[BLE] Security request — accepting\n");
    return true;
  }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl) override {
    if (auth_cmpl.success) {
      printf("[BLE] Authentication complete — bonded\n");
    } else {
      printf("[BLE] Authentication FAILED (reason=%d)\n", auth_cmpl.fail_reason);
    }
  }

  bool onConfirmPIN(uint32_t pin) override {
#if BLE_USE_PASSKEY
    printf("[BLE] Confirm PIN: %06u — accepted\n", (unsigned)pin);
#else
    (void)pin;
#endif
    return true;
  }
};

// ---------------------------------------------------------------------------
// Characteristic callbacks
// ---------------------------------------------------------------------------

// Saved Codes — compact JSON (index + name) to stay under 600-byte BLE limit.
class SavedCodesCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* pCharacteristic) override {
    String json = getSavedCodesJsonCompact();
    pCharacteristic->setValue(json.c_str());
    printf("[BLE] Saved codes read (%u bytes)\n", (unsigned)json.length());
  }
};

// Send Command — the client writes one byte (the saved-code index).
class SendCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    std::string val = pCharacteristic->getValue();
    if (val.size() < 1) {
      setStatus("ERR:empty write");
      return;
    }

    int index = (uint8_t)val[0];
    String name;
    bool ok = sendSavedCode(index, name);

    String status;
    if (ok) {
      status = "OK:" + (name.length() > 0 ? name : String(index));
    } else {
      status = "ERR:index " + String(index);
    }
    setStatus(status);
    printf("[BLE] Send command: index=%d -> %s\n", index, status.c_str());
  }
};

// Schedule — JSON write: {"delay_seconds": N, "command": "Name"} to arm, or {"heartbeat": true} keepalive.
class ScheduleCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    std::string val = pCharacteristic->getValue();
    if (val.size() == 0) {
      setStatus("ERR:schedule empty");
      return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, val.c_str(), val.size());
    if (err) {
      setStatus("ERR:schedule json");
      printf("[BLE] Schedule: invalid JSON\n");
      return;
    }

    if (doc["heartbeat"].is<bool>() && doc["heartbeat"].as<bool>()) {
      printf("[BLE] Schedule: heartbeat\n");
      return;
    }

    if (doc["delay_seconds"].is<int>() && doc["command"].is<const char*>()) {
      int sec = doc["delay_seconds"].as<int>();
      const char* cmd = doc["command"].as<const char*>();
      if (sec <= 0 || !cmd || !*cmd) {
        setStatus("ERR:schedule invalid");
        return;
      }
      if ((uint32_t)sec > BLE_SCHEDULE_DELAY_SEC_MAX) {
        setStatus("ERR:schedule delay too long");
        printf("[BLE] Schedule: delay_seconds %d exceeds max %u\n", sec, (unsigned)BLE_SCHEDULE_DELAY_SEC_MAX);
        return;
      }
      size_t len = strlen(cmd);
      if (len >= BLE_SCHEDULE_CMD_NAME_MAX) {
        setStatus("ERR:schedule name long");
        return;
      }
      strncpy(scheduledCommandName, cmd, BLE_SCHEDULE_CMD_NAME_MAX - 1);
      scheduledCommandName[BLE_SCHEDULE_CMD_NAME_MAX - 1] = '\0';
      scheduledDelayMs = (uint32_t)sec * 1000UL;
      scheduledArmed = true;
      printf("[BLE] Schedule: armed %s in %u s\n", scheduledCommandName, (unsigned)sec);
      setStatus("OK:scheduled");
      return;
    }

    setStatus("ERR:schedule format");
  }
};

// ---------------------------------------------------------------------------
// Static callback instances
// ---------------------------------------------------------------------------
static IRServerCallbacks    serverCb;
static IRSecurityCallbacks  securityCb;
static SavedCodesCallbacks  savedCodesCb;
static SendCommandCallbacks sendCommandCb;
static ScheduleCallbacks    scheduleCb;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void setupBLE() {
  printf("[BLE] Initializing BLE...\n");

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setMTU(512);

  // Security: bonding + encryption. Just Works (no passkey) or passkey entry when BLE_USE_PASSKEY.
  BLEDevice::setEncryptionLevel(BLE_USE_PASSKEY ? ESP_BLE_SEC_ENCRYPT_MITM : ESP_BLE_SEC_ENCRYPT);
  BLEDevice::setSecurityCallbacks(&securityCb);

  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(BLE_USE_PASSKEY ? ESP_LE_AUTH_REQ_SC_MITM_BOND : ESP_LE_AUTH_REQ_SC_BOND);
  pSecurity->setCapability(BLE_USE_PASSKEY ? ESP_IO_CAP_OUT : ESP_IO_CAP_NONE);  // OUT = display passkey; NONE = Just Works
  if (BLE_USE_PASSKEY) {
    pSecurity->setStaticPIN(BLE_PASSKEY);
  }
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(&serverCb);

  // --- Service ---
  BLEService* pService = pServer->createService(BLE_IR_SERVICE_UUID);

  // Characteristic permissions: ENC_MITM when passkey used, ENC only for Just Works (no MITM).
  const uint32_t perm_read  = BLE_USE_PASSKEY ? ESP_GATT_PERM_READ_ENC_MITM  : ESP_GATT_PERM_READ_ENCRYPTED;
  const uint32_t perm_write = BLE_USE_PASSKEY ? ESP_GATT_PERM_WRITE_ENC_MITM : ESP_GATT_PERM_WRITE_ENCRYPTED;

  // Saved Codes (Read)
  pSavedChar = pService->createCharacteristic(
      BLE_CHAR_SAVED_UUID,
      BLECharacteristic::PROPERTY_READ);
  pSavedChar->setAccessPermissions(perm_read);
  pSavedChar->setCallbacks(&savedCodesCb);

  // Send Command (Write)
  pSendChar = pService->createCharacteristic(
      BLE_CHAR_SEND_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  pSendChar->setAccessPermissions(perm_write);
  pSendChar->setCallbacks(&sendCommandCb);

  // Status (Read + Notify)
  pStatusChar = pService->createCharacteristic(
      BLE_CHAR_STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pStatusChar->setAccessPermissions(perm_read);
  pStatusChar->addDescriptor(new BLE2902());
  pStatusChar->setValue("READY");

  // Schedule (Write)
  pScheduleChar = pService->createCharacteristic(
      BLE_CHAR_SCHEDULE_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  pScheduleChar->setAccessPermissions(perm_write);
  pScheduleChar->setCallbacks(&scheduleCb);

  pService->start();

  // --- Advertising ---
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_IR_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // helps with iPhone connectivity
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  printf("[BLE] Advertising started as \"%s\"\n", BLE_DEVICE_NAME);
}

bool getScheduleCountdown(uint32_t* out_seconds_remaining, char* out_command_name, size_t name_max) {
  // Countdown only runs when client is disconnected.
  if (!scheduledArmed || scheduledCommandName[0] == '\0' || deviceConnected ||
      !out_seconds_remaining || !out_command_name || name_max == 0) {
    return false;
  }
  unsigned long elapsed = millis() - countdownStartMs;
  if (elapsed >= scheduledDelayMs) {
    return false;  // already expired, about to fire
  }
  *out_seconds_remaining = (scheduledDelayMs - (uint32_t)elapsed + 999) / 1000;
  strncpy(out_command_name, scheduledCommandName, name_max - 1);
  out_command_name[name_max - 1] = '\0';
  return true;
}

void loopBLE() {
  // Delayed command: countdown starts on disconnect; after delay_seconds run the scheduled command once.
  if (scheduledArmed && scheduledCommandName[0] != '\0' && !deviceConnected &&
      (millis() - countdownStartMs) >= scheduledDelayMs) {
    scheduledArmed = false;
    int idx = getSavedCodeIndexByName(scheduledCommandName);
    if (idx >= 0) {
      String name;
      if (sendSavedCode(idx, name)) {
        setStatus("OK:scheduled " + name);
        printf("[BLE] Scheduled command executed: %s\n", name.c_str());
      } else {
        setStatus("ERR:scheduled send");
      }
    } else {
      setStatus("ERR:scheduled not found");
      printf("[BLE] Scheduled command not found: %s\n", scheduledCommandName);
    }
  }
}
