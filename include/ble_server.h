#ifndef BLE_SERVER_H
#define BLE_SERVER_H

// BLE GATT service UUIDs — use these in client apps to discover the IR Blaster.
#define BLE_IR_SERVICE_UUID      "e97a0001-c116-4a63-a60f-0e9b4d3648f3"
#define BLE_CHAR_SAVED_UUID      "e97a0002-c116-4a63-a60f-0e9b4d3648f3"
#define BLE_CHAR_SEND_UUID       "e97a0003-c116-4a63-a60f-0e9b4d3648f3"
#define BLE_CHAR_STATUS_UUID     "e97a0004-c116-4a63-a60f-0e9b4d3648f3"
#define BLE_CHAR_SCHEDULE_UUID   "e97a0005-c116-4a63-a60f-0e9b4d3648f3"

#include "secrets.h"

#define BLE_DEVICE_NAME          "IR Blaster"

// Pairing: "Just Works" (no passkey) by default.
// To require a passkey, #define BLE_USE_PASSKEY 1 in src/secrets.h.
#ifndef BLE_USE_PASSKEY
#define BLE_USE_PASSKEY           0
#endif

#define BLE_SCHEDULE_CMD_NAME_MAX 32   // max length of scheduled command name
// Max delay_seconds so that delay_seconds * 1000 fits in uint32_t (avoids overflow).
#define BLE_SCHEDULE_DELAY_SEC_MAX  (4294967u)  // UINT32_MAX / 1000

// Call from setup() after IR and NVS are ready.
void setupBLE();

// Call from loop().  Currently a no-op (BLE is callback-driven).
void loopBLE();

// If schedule is armed, return true and fill seconds remaining and command name; otherwise return false.
bool getScheduleCountdown(uint32_t* out_seconds_remaining, char* out_command_name, size_t name_max);

#endif // BLE_SERVER_H
