#ifndef BLE_SERVER_H
#define BLE_SERVER_H

// BLE GATT service UUIDs — use these in client apps to discover the IR Blaster.
#define BLE_IR_SERVICE_UUID      "e97a0001-c116-4a63-a60f-0e9b4d3648f3"
#define BLE_CHAR_SAVED_UUID      "e97a0002-c116-4a63-a60f-0e9b4d3648f3"
#define BLE_CHAR_SEND_UUID       "e97a0003-c116-4a63-a60f-0e9b4d3648f3"
#define BLE_CHAR_STATUS_UUID     "e97a0004-c116-4a63-a60f-0e9b4d3648f3"

#define BLE_DEVICE_NAME          "IR Blaster"
#define BLE_PASSKEY              123456   // shown on Serial during first pairing

// Auto-send on connect and after disconnect timeout (Blaster Mac Client).
#define BLE_CONNECT_SEND_INDEX    5                         // "On"
#define BLE_TIMEOUT_SEND_INDEX    0                         // "Off"
#define BLE_DISCONNECT_TIMEOUT_MS (15UL * 60UL * 1000UL)   // 15 minutes

// Call from setup() after IR and NVS are ready.
void setupBLE();

// Call from loop().  Currently a no-op (BLE is callback-driven).
void loopBLE();

#endif // BLE_SERVER_H
