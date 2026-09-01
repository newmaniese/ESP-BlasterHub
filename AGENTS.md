# AGENTS.md

Project context for AI assistants and new contributors.

## Project summary

ESP32-C3 IR Blaster firmware and web UI:
- Receives IR codes (GPIO 10), sends IR (GPIO 4).
- Serves a LittleFS-hosted frontend over WiFi.
- Stores saved IR codes in NVS across reboots.
- Supports HTTP API and WebSocket live updates.
- Token-authorized, encrypted non-bonded BLE GATT server for stored commands.

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
make build
```

When to run what:
- Firmware-only changes: `make upload`
- Frontend (`data/`) changes: `make fs`
- First-time setup: `make build`

Equivalent PlatformIO: `pio run --target upload`, then `buildfs`, then `uploadfs`.

Transmit-only (no IR receiver): set `IR_RECV_ENABLED=0` in `.env` before building (`scripts/pio_env_flags.py` passes `-DIR_RECV_ENABLED=…`).
Default IR burst count: `IR_SEND_REPEAT` in `.env` (1–20; default 1).
BLE-only (WiFi radio off, no HTTP server): leave `WIFI_SSID` / `WIFI_PASS` empty (or omit them) in `.env`. Use when no configured network is reachable — WiFi association retries share the radio with BLE and destabilise the link.

Copy `.env` BLE name/token to the local Mac client: `make sync-mac-client`.

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

BLE integration tests (device and shared token required):

```bash
DEVICE_BLE_NAME="IR Blaster" DEVICE_BLE_AUTH_TOKEN="<token>" \
  pytest test/integration/test_ble.py
```

## Documentation index

- Main index: `README.md`
- Wiring: `docs/wiring.md`
- Web UI and API: `docs/web-interface.md`
- Bluetooth (BLE): `docs/bluetooth.md`
- Serial logging: `docs/serial-monitor.md`
- Troubleshooting: `docs/troubleshooting.md`
