// BLE GATT server for the IR Blaster.
//
// Uses the Arduino-ESP32 built-in BLE library (Bluedroid stack).
//
// Exposes five characteristics behind per-connection encryption:
//   - Authenticate (Read/Write) — shared-token session authorization
//   - Saved Codes  (Read)   — JSON array of stored IR commands
//   - Send Command (Write)  — write a single byte (NVS index) to send that code
//   - Status  (Read/Notify) — notifies the result string after a send
//                             ("OK:<name>" or "ERR:…"); on connect its value is
//                             the reconnect-countdown snapshot JSON
//   - Schedule     (Write)  — configure the disconnect delay; heartbeat
//
// Security: non-bonded LE Secure Connections plus an application token. Avoiding
// persistent bond keys prevents macOS and the ESP32 from deadlocking when one
// side loses its copy. Passkey mode adds MITM protection but prompts each session.
// Auto-reconnect: advertising restarts on disconnect so the client reconnects.
//
// Half-open links: if the stack reports "connected" but no GATT traffic arrives
// for BLE_LINK_IDLE_TIMEOUT_MS, force-disconnect and restart advertising so the
// client can reconnect and the disconnect countdown can start. The teardown is
// keyed to the connection that was judged idle, since a client reconnecting in
// the meantime must not be the one dropped.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ble_server.h"

#if BLE_USE_PASSKEY && !defined(BLE_PASSKEY)
#error "BLE_PASSKEY must be defined (e.g. in secrets.h) when BLE_USE_PASSKEY is enabled"
#elif BLE_USE_PASSKEY && (BLE_PASSKEY == 123456)
#error "BLE_PASSKEY must be changed from the default 123456 for security"
#endif

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
static BLECharacteristic* pAuthChar    = nullptr;
static volatile bool      deviceConnected = false;
static volatile bool      sessionAuthorized = false;

// Disconnect-delayed command: configure while connected; countdown starts on disconnect.
static char     scheduledCommandName[BLE_SCHEDULE_CMD_NAME_MAX] = "";
static uint32_t scheduledDelayMs   = 0;
static unsigned long countdownStartMs = 0;
static bool     scheduleConfigured = false;
static bool     countdownActive    = false;
static bool     countdownExpiredSinceLastConnect = false;
static char     reconnectCountdownState[12] = "none";
static uint32_t reconnectCountdownRemainingSec = 0;
static char     reconnectCountdownCommand[BLE_SCHEDULE_CMD_NAME_MAX] = "";
static SemaphoreHandle_t scheduleStateMutex = nullptr;

// Half-open link watchdog state. The epoch is bumped on every connect so the
// watchdog can tell whether the link it judged idle is still the current one.
static volatile unsigned long lastGattActivityMs = 0;
static volatile bool          linkLostInProgress = false;
static volatile uint32_t      connectionEpoch = 0;
static volatile uint16_t      currentConnId = 0;

static bool initScheduleStateMutex() {
  if (scheduleStateMutex != nullptr) return true;
  scheduleStateMutex = xSemaphoreCreateMutex();
  if (scheduleStateMutex == nullptr) {
    printf("[BLE] Failed to create schedule mutex\n");
    return false;
  }
  return true;
}

class ScheduleStateLock {
public:
  ScheduleStateLock() : locked(false) {
    if (!initScheduleStateMutex()) return;
    locked = (xSemaphoreTake(scheduleStateMutex, portMAX_DELAY) == pdTRUE);
    if (!locked) {
      printf("[BLE] Failed to lock schedule mutex\n");
    }
  }

  ~ScheduleStateLock() {
    if (locked) xSemaphoreGive(scheduleStateMutex);
  }

  explicit operator bool() const {
    return locked;
  }

private:
  bool locked;
};

static void cancelCountdownLocked() {
  countdownActive = false;
  countdownStartMs = 0;
}

static void startCountdownLocked() {
  if (!scheduleConfigured || scheduledCommandName[0] == '\0') {
    return;
  }
  countdownStartMs = millis();
  countdownActive = true;
  countdownExpiredSinceLastConnect = false;
}

static void snapshotCountdownOnConnectLocked() {
  strcpy(reconnectCountdownState, "none");
  reconnectCountdownRemainingSec = 0;
  reconnectCountdownCommand[0] = '\0';

  if (scheduledCommandName[0] != '\0') {
    strncpy(
        reconnectCountdownCommand,
        scheduledCommandName,
        BLE_SCHEDULE_CMD_NAME_MAX - 1);
    reconnectCountdownCommand[BLE_SCHEDULE_CMD_NAME_MAX - 1] = '\0';
  }

  if (countdownActive) {
    const uint32_t elapsed = (uint32_t)(millis() - countdownStartMs);
    if (elapsed < scheduledDelayMs) {
      strcpy(reconnectCountdownState, "interrupted");
      reconnectCountdownRemainingSec =
          (scheduledDelayMs - elapsed + 999) / 1000;
    } else {
      strcpy(reconnectCountdownState, "expired");
    }
  } else if (countdownExpiredSinceLastConnect) {
    strcpy(reconnectCountdownState, "expired");
  }

  countdownExpiredSinceLastConnect = false;
  cancelCountdownLocked();
}

static String reconnectSnapshotJsonLocked() {
  JsonDocument doc;
  doc["state"] = reconnectCountdownState;
  doc["remaining_seconds"] = reconnectCountdownRemainingSec;
  doc["command"] = reconnectCountdownCommand;
  String payload;
  serializeJson(doc, payload);
  return payload;
}

static void noteGattActivity() {
  lastGattActivityMs = millis();
}

static void setStatus(const String& msg);

static bool tokenMatches(const std::string& candidate) {
  const char* expected = BLE_AUTH_TOKEN;
  const size_t expectedLen = strlen(expected);
  if (candidate.size() != expectedLen) return false;

  uint8_t difference = 0;
  for (size_t i = 0; i < expectedLen; ++i) {
    difference |= static_cast<uint8_t>(candidate[i]) ^
                  static_cast<uint8_t>(expected[i]);
  }
  return difference == 0;
}

static bool requireAuthorized(const char* operation) {
  if (sessionAuthorized) return true;
  printf("[BLE] Rejected unauthenticated %s\n", operation);
  setStatus("ERR:unauthorized");
  return false;
}

// Helper: set Status characteristic and notify if connected.
static void setStatus(const String& msg) {
  pStatusChar->setValue(msg.c_str());
  if (deviceConnected) {
    pStatusChar->notify();
  }
}

// Shared teardown for real disconnects and half-open watchdog trips: start the
// countdown, restart advertising, and (for a half-open link) drop the stale
// connection first so the stack will advertise again.
static void handleLinkLost(const char* reason, bool forceDisconnect,
                           uint32_t expectedEpoch = 0, uint16_t staleConnId = 0) {
  if (linkLostInProgress) return;
  linkLostInProgress = true;

  // The client can reconnect between the watchdog's idle check and this call.
  // Tearing down then would drop the fresh, healthy link rather than the stale
  // one, so bail out and let the new connection live.
  if (forceDisconnect && connectionEpoch != expectedEpoch) {
    linkLostInProgress = false;
    return;
  }

  deviceConnected = false;
  sessionAuthorized = false;

  bool countdownStarted = false;
  uint32_t delaySec = 0;
  char commandCopy[BLE_SCHEDULE_CMD_NAME_MAX] = "";
  {
    ScheduleStateLock lock;
    if (lock) {
      // A forced drop is followed by the stack's own onDisconnect, so only the
      // first link-loss event may set the countdown start time.
      const bool alreadyCounting = countdownActive;
      if (!alreadyCounting) {
        startCountdownLocked();
      }
      countdownStarted = countdownActive && !alreadyCounting;
      delaySec = (uint32_t)(scheduledDelayMs / 1000UL);
      strncpy(commandCopy, scheduledCommandName, BLE_SCHEDULE_CMD_NAME_MAX - 1);
      commandCopy[BLE_SCHEDULE_CMD_NAME_MAX - 1] = '\0';
    }
  }

  // Drop the connection the watchdog judged idle by id, so a reconnect that
  // races this teardown is never the one that gets closed.
  if (forceDisconnect && pServer != nullptr) {
    pServer->disconnect(staleConnId);
  }
  BLEDevice::startAdvertising();

  printf("[BLE] Link lost (%s) — restarting advertising\n", reason);
  if (countdownStarted) {
    printf("[BLE] Schedule: countdown started (%u s until %s)\n",
           (unsigned)delaySec, commandCopy);
  }

  linkLostInProgress = false;
}

// ---------------------------------------------------------------------------
// Server callbacks — connect / disconnect
// ---------------------------------------------------------------------------
class IRServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    sessionAuthorized = false;
    if (pAuthChar != nullptr) {
      pAuthChar->setValue("AUTH_REQUIRED");
    }
    currentConnId = pServer->getConnId();
    connectionEpoch++;
    noteGattActivity();
    String snapshot;
    {
      ScheduleStateLock lock;
      if (lock) {
        // Preserve the device-side countdown result for the newly connected
        // client to read before cancelling the live timer.
        snapshotCountdownOnConnectLocked();
        snapshot = reconnectSnapshotJsonLocked();
      }
    }
    // Park the reconnect snapshot in Status before any command result can
    // overwrite it. Publish without notifying; the client reads it once after
    // authorizing the connection.
    if (snapshot.length() > 0) {
      pStatusChar->setValue(snapshot.c_str());
    }
    printf("[BLE] Client connected\n");
  }

  void onDisconnect(BLEServer* pServer) override {
    (void)pServer;
    handleLinkLost("client disconnected", false);
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
      printf("[BLE] Secure connection established (non-bonded)\n");
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

class AuthCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* pCharacteristic) override {
    noteGattActivity();
    pCharacteristic->setValue(sessionAuthorized ? "OK" : "AUTH_REQUIRED");
  }

  void onWrite(BLECharacteristic* pCharacteristic) override {
    noteGattActivity();
    const std::string value = pCharacteristic->getValue();
    sessionAuthorized = tokenMatches(value);
    pCharacteristic->setValue(sessionAuthorized ? "OK" : "ERR");
    printf("[BLE] Session authorization %s\n",
           sessionAuthorized ? "accepted" : "rejected");
  }
};

// Saved Codes — compact JSON (index + name) to stay under 600-byte BLE limit.
class SavedCodesCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* pCharacteristic) override {
    noteGattActivity();
    if (!requireAuthorized("Saved Codes read")) {
      pCharacteristic->setValue("ERR:unauthorized");
      return;
    }
    String json = getSavedCodesJsonCompact();
    pCharacteristic->setValue(json.c_str());
    printf("[BLE] Saved codes read (%u bytes)\n", (unsigned)json.length());
  }
};

// Send Command — the client writes one byte (the saved-code index).
class SendCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    noteGattActivity();
    if (!requireAuthorized("Send Command write")) return;
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

// Schedule — JSON write: {"delay_seconds": N, "command": "Name"} configures the
// command that runs Delay seconds after BLE disconnect (countdown starts on
// disconnect), or {"heartbeat": true} as a keepalive for the idle watchdog.
class ScheduleCallbacks : public BLECharacteristicCallbacks {
public:
  void onWrite(BLECharacteristic* pCharacteristic) override {
    noteGattActivity();
    if (!requireAuthorized("Schedule write")) return;
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
      handleHeartbeat();
      return;
    }

    if (doc["delay_seconds"].is<int>() && doc["command"].is<const char*>()) {
      handleCommand(doc["delay_seconds"].as<int>(), doc["command"].as<const char*>());
      return;
    }

    setStatus("ERR:schedule format");
  }

private:
  // Keepalive only: the idle watchdog was already reset by noteGattActivity().
  // If a countdown is somehow running (e.g. a half-open link that recovered),
  // postpone it rather than letting it fire under a live client.
  void handleHeartbeat() {
    printf("[BLE] Heartbeat\n");
    ScheduleStateLock lock;
    if (!lock) {
      setStatus("ERR:schedule lock");
      return;
    }
    if (countdownActive) {
      countdownStartMs = millis();
    }
  }

  void handleCommand(int sec, const char* cmd) {
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

    char commandCopy[BLE_SCHEDULE_CMD_NAME_MAX];
    strncpy(commandCopy, cmd, BLE_SCHEDULE_CMD_NAME_MAX - 1);
    commandCopy[BLE_SCHEDULE_CMD_NAME_MAX - 1] = '\0';

    uint32_t delayMs = (uint32_t)sec * 1000UL;

    {
      ScheduleStateLock lock;
      if (!lock) {
        setStatus("ERR:schedule lock");
        return;
      }
      strncpy(scheduledCommandName, commandCopy, BLE_SCHEDULE_CMD_NAME_MAX - 1);
      scheduledCommandName[BLE_SCHEDULE_CMD_NAME_MAX - 1] = '\0';
      scheduledDelayMs = delayMs;
      scheduleConfigured = true;
      cancelCountdownLocked();
    }
    printf("[BLE] Schedule: configured %s after %u s disconnect\n", commandCopy, (unsigned)sec);
    setStatus("OK:scheduled");
  }
};

// ---------------------------------------------------------------------------
// Static callback instances
// ---------------------------------------------------------------------------
static IRServerCallbacks    serverCb;
static IRSecurityCallbacks  securityCb;
static AuthCallbacks        authCb;
static SavedCodesCallbacks  savedCodesCb;
static SendCommandCallbacks sendCommandCb;
static ScheduleCallbacks    scheduleCb;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void setupBLESecurity() {
  // Encrypt every connection, but never persist an SMP bond. Authorization is
  // provided by BLE_AUTH_TOKEN after encryption succeeds.
  BLEDevice::setEncryptionLevel(BLE_USE_PASSKEY ? ESP_BLE_SEC_ENCRYPT_MITM : ESP_BLE_SEC_ENCRYPT);
  BLEDevice::setSecurityCallbacks(&securityCb);

  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(BLE_USE_PASSKEY ? ESP_LE_AUTH_REQ_SC_MITM : ESP_LE_AUTH_REQ_SC_ONLY);
  pSecurity->setCapability(BLE_USE_PASSKEY ? ESP_IO_CAP_OUT : ESP_IO_CAP_NONE);  // OUT = display passkey; NONE = Just Works
#if BLE_USE_PASSKEY
  pSecurity->setStaticPIN(BLE_PASSKEY);
#endif
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
}

static void setupBLECharacteristics(BLEService* pService) {
  // Characteristic permissions: ENC_MITM when passkey used, ENC only for Just Works (no MITM).
  const uint32_t perm_read  = BLE_USE_PASSKEY ? ESP_GATT_PERM_READ_ENC_MITM  : ESP_GATT_PERM_READ_ENCRYPTED;
  const uint32_t perm_write = BLE_USE_PASSKEY ? ESP_GATT_PERM_WRITE_ENC_MITM : ESP_GATT_PERM_WRITE_ENCRYPTED;

  // Authenticate (Read + Write). The client writes the shared token once after
  // each connection and reads back OK before accessing any other characteristic.
  pAuthChar = pService->createCharacteristic(
      BLE_CHAR_AUTH_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pAuthChar->setAccessPermissions(perm_read | perm_write);
  pAuthChar->setCallbacks(&authCb);
  pAuthChar->setValue("AUTH_REQUIRED");

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
}

static void setupBLEAdvertising() {
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_IR_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // helps with iPhone connectivity
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  printf("[BLE] Advertising started as \"%s\"\n", BLE_DEVICE_NAME);
  printf("[BLE] Link idle timeout %lu s\n", (unsigned long)(BLE_LINK_IDLE_TIMEOUT_MS / 1000UL));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void setupBLE() {
  printf("[BLE] Initializing BLE...\n");
  if (!initScheduleStateMutex()) {
    printf("[BLE] WARNING: schedule mutex unavailable; scheduling operations may fail\n");
  }

#if BLE_USE_PASSKEY
  if (BLE_PASSKEY > 999999) {
    printf("[BLE] WARNING: BLE_PASSKEY %u is greater than 999999; only the last 6 digits will be used.\n", (unsigned)BLE_PASSKEY);
  }
#endif

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setMTU(512);

  setupBLESecurity();

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(&serverCb);

  // --- Service ---
  BLEService* pService = pServer->createService(BLE_IR_SERVICE_UUID);

  setupBLECharacteristics(pService);

  pService->start();

  noteGattActivity();
  setupBLEAdvertising();
}

bool getScheduleCountdown(uint32_t* out_seconds_remaining, char* out_command_name, size_t name_max) {
  if (!out_seconds_remaining || !out_command_name || name_max == 0) {
    return false;
  }

  bool counting = false;
  uint32_t delayMs = 0;
  unsigned long startMs = 0;
  char commandCopy[BLE_SCHEDULE_CMD_NAME_MAX];
  {
    ScheduleStateLock lock;
    if (!lock) return false;
    counting = countdownActive;
    delayMs = scheduledDelayMs;
    startMs = countdownStartMs;
    strncpy(commandCopy, scheduledCommandName, BLE_SCHEDULE_CMD_NAME_MAX - 1);
    commandCopy[BLE_SCHEDULE_CMD_NAME_MAX - 1] = '\0';
  }

  if (!counting || commandCopy[0] == '\0') {
    return false;
  }

  unsigned long elapsed = millis() - startMs;
  if (elapsed >= delayMs) {
    return false;  // already expired, about to fire
  }
  *out_seconds_remaining = (delayMs - (uint32_t)elapsed + 999) / 1000;
  strncpy(out_command_name, commandCopy, name_max - 1);
  out_command_name[name_max - 1] = '\0';
  return true;
}

void loopBLE() {
  // Half-open watchdog: the stack still reports a client, but no GATT traffic
  // has arrived for too long, so treat the link as dead.
  // Sample the activity stamp before the clock. Read the other way round, a GATT
  // callback landing between the two reads leaves the stamp ahead of "now" and
  // the unsigned subtraction wraps to ~49 days, tripping the watchdog on a
  // perfectly healthy link — which looks like a client that connects and is
  // dropped a second later. This order also keeps millis() wrap-around correct.
  const unsigned long lastActivity = lastGattActivityMs;
  const unsigned long idleMs = millis() - lastActivity;
  if (deviceConnected && idleMs >= BLE_LINK_IDLE_TIMEOUT_MS) {
    char reason[64];
    snprintf(reason, sizeof(reason), "GATT idle timeout, %lus since traffic",
             (unsigned long)(idleMs / 1000UL));
    handleLinkLost(reason, true, connectionEpoch, currentConnId);
  }

  bool shouldRun = false;
  char commandToRun[BLE_SCHEDULE_CMD_NAME_MAX] = "";

  // Fire once when disconnect countdown elapses. Sample millis() under the lock.
  {
    ScheduleStateLock lock;
    if (!lock) return;
    const unsigned long nowMs = millis();
    if (countdownActive && !deviceConnected && scheduledCommandName[0] != '\0' &&
        (nowMs - countdownStartMs) >= scheduledDelayMs) {
      countdownActive = false;
      countdownExpiredSinceLastConnect = true;
      strncpy(commandToRun, scheduledCommandName, BLE_SCHEDULE_CMD_NAME_MAX - 1);
      commandToRun[BLE_SCHEDULE_CMD_NAME_MAX - 1] = '\0';
      shouldRun = true;
    }
  }

  if (!shouldRun) return;

  int idx = getSavedCodeIndexByName(commandToRun);
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
    printf("[BLE] Scheduled command not found: %s\n", commandToRun);
  }
}
