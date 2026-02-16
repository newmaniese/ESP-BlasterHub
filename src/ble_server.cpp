// BLE GATT server for the IR Blaster.
//
// Uses the Arduino-ESP32 built-in BLE library (Bluedroid stack).
//
// Exposes three characteristics behind bonded encryption:
//   - Saved Codes  (Read)   — JSON array of stored IR commands
//   - Send Command (Write)  — write a single byte (NVS index) to send that code
//   - Status       (Notify) — result string after a send ("OK:<name>" or "ERR:…")
//
// Security: bonding + MITM + Secure Connections, passkey displayed on Serial.
// Auto-reconnect: advertising restarts on disconnect so the client reconnects.

#include <Arduino.h>
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
extern bool   sendSavedCode(int index, String &outName);

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static BLEServer*         pServer     = nullptr;
static BLECharacteristic* pSavedChar  = nullptr;
static BLECharacteristic* pSendChar   = nullptr;
static BLECharacteristic* pStatusChar = nullptr;
static bool               deviceConnected = false;
static unsigned long      disconnectTime = 0;
static bool               offSentAfterDisconnect = true;   // true so we don't fire on boot
static bool               hasEverConnected = false;

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
    deviceConnected = true;
    hasEverConnected = true;
    offSentAfterDisconnect = true;   // reset; only send Off after next disconnect timeout
    printf("[BLE] Client connected\n");

    // Auto-send "On" when client connects (Blaster Mac Client).
    String name;
    if (sendSavedCode(BLE_CONNECT_SEND_INDEX, name)) {
      String status = "OK:" + (name.length() > 0 ? name : String(BLE_CONNECT_SEND_INDEX));
      setStatus(status);
      printf("[BLE] Auto-send on connect: %s\n", status.c_str());
    } else {
      setStatus("ERR:connect send");
      printf("[BLE] Auto-send on connect failed\n");
    }
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    disconnectTime = millis();
    offSentAfterDisconnect = false;
    printf("[BLE] Client disconnected — restarting advertising\n");
    BLEDevice::startAdvertising();
  }
};

// ---------------------------------------------------------------------------
// Security callbacks — passkey and authentication
// ---------------------------------------------------------------------------
class IRSecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override {
    printf("[BLE] *** Pairing passkey: %06u — enter this on the client ***\n", (unsigned)BLE_PASSKEY);
    return BLE_PASSKEY;
  }

  void onPassKeyNotify(uint32_t pass_key) override {
    printf("[BLE] *** Pairing passkey (display): %06u ***\n", (unsigned)pass_key);
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
    printf("[BLE] Confirm PIN: %06u — accepted\n", (unsigned)pin);
    return true;
  }
};

// ---------------------------------------------------------------------------
// Characteristic callbacks
// ---------------------------------------------------------------------------

// Saved Codes — refreshed from NVS on every read.
class SavedCodesCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* pCharacteristic) override {
    String json = getSavedCodesJson();
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

// ---------------------------------------------------------------------------
// Static callback instances
// ---------------------------------------------------------------------------
static IRServerCallbacks    serverCb;
static IRSecurityCallbacks  securityCb;
static SavedCodesCallbacks  savedCodesCb;
static SendCommandCallbacks sendCommandCb;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void setupBLE() {
  printf("[BLE] Initializing BLE...\n");

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setMTU(512);

  // Security: bonding + MITM + Secure Connections
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
  BLEDevice::setSecurityCallbacks(&securityCb);

  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  pSecurity->setCapability(ESP_IO_CAP_OUT);  // display-only (passkey shown on Serial)
  pSecurity->setStaticPIN(BLE_PASSKEY);
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(&serverCb);

  // --- Service ---
  BLEService* pService = pServer->createService(BLE_IR_SERVICE_UUID);

  // Saved Codes (Read, encrypted + MITM)
  pSavedChar = pService->createCharacteristic(
      BLE_CHAR_SAVED_UUID,
      BLECharacteristic::PROPERTY_READ);
  pSavedChar->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  pSavedChar->setCallbacks(&savedCodesCb);

  // Send Command (Write, encrypted + MITM)
  pSendChar = pService->createCharacteristic(
      BLE_CHAR_SEND_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  pSendChar->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  pSendChar->setCallbacks(&sendCommandCb);

  // Status (Read + Notify, encrypted + MITM)
  pStatusChar = pService->createCharacteristic(
      BLE_CHAR_STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pStatusChar->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  pStatusChar->addDescriptor(new BLE2902());
  pStatusChar->setValue("READY");

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

void loopBLE() {
  // After 15 min with no BLE client, auto-send "Off" once (Blaster Mac Client).
  if (!deviceConnected && hasEverConnected && !offSentAfterDisconnect &&
      (millis() - disconnectTime >= BLE_DISCONNECT_TIMEOUT_MS)) {
    offSentAfterDisconnect = true;
    String name;
    if (sendSavedCode(BLE_TIMEOUT_SEND_INDEX, name)) {
      printf("[BLE] Auto-send after disconnect timeout: %s\n", name.length() > 0 ? name.c_str() : "Off");
    }
  }
}
