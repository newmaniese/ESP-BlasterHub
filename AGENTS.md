# AGENTS.md

Project context for AI assistants and new contributors.

## Project summary

ESP32-C3 IR Blaster firmware and web UI:
- Receives IR codes (GPIO 10), sends IR (GPIO 4).
- Serves a LittleFS-hosted frontend over WiFi.
- Stores saved IR codes in NVS across reboots.
- Supports HTTP API and WebSocket live updates.
- BLE GATT server for sending stored commands from a bonded computer (NimBLE).

## Stack and dependencies

- PlatformIO + Arduino framework
- ESP32-C3 (`esp32-c3-devkitm-1`)
- `IRremoteESP8266`
- `ArduinoJson`
- `ESPAsyncWebServer` + `AsyncTCP`
- Built-in Arduino-ESP32 BLE (Bluedroid GATT server)
- LittleFS for static frontend assets

## Key paths

- Firmware entry: `src/main.cpp`
- Shared helpers: `include/ir_utils.h`, `src/ir_utils.cpp`
- BLE server: `include/ble_server.h`, `src/ble_server.cpp`
- Frontend: `data/index.html`, `data/app.css`, `data/app.js`
- PlatformIO config: `platformio.ini`
- Documentation: `docs/`

## Build and install

Full install sequence (firmware + frontend):

```bash
pio run --target upload
pio run --target buildfs
pio run --target uploadfs
```

When to run what:
- Firmware-only changes: `pio run --target upload`
- Frontend (`data/`) changes: run `buildfs` + `uploadfs`
- First-time setup: run all three commands above

## Testing

On-device unit tests:

```bash
pio test -e esp32c3-test
```

Host integration tests (device must be running):

```bash
pip install -r requirements-test.txt
DEVICE_IP=http://<device-ip> pytest test/integration/test_api.py
```

BLE integration tests (device must be paired/bonded):

```bash
DEVICE_BLE_NAME="IR Blaster" pytest test/integration/test_ble.py
```

## Documentation index

- Main index: `README.md`
- Wiring: `docs/wiring.md`
- Web UI and API: `docs/web-interface.md`
- Bluetooth (BLE): `docs/bluetooth.md`
- Serial logging: `docs/serial-monitor.md`
- Troubleshooting: `docs/troubleshooting.md`
