# ESP32-C3 IR Blaster - Troubleshooting

Common issues and quick fixes.

## Build and upload

### Upload fails or board is not found

- Run `pio device list` and confirm your ESP32-C3 serial port.
- Make sure your USB cable supports data (some charge-only cables fail here).
- Try a different USB port and reconnect the board.
- Confirm the board target in `platformio.ini` is `esp32-c3-devkitm-1`.
- If your board requires it, hold the boot button while upload starts.

## Serial monitor

### No output in monitor

- Start the monitor with `pio device monitor`.
- Wait 5-10 seconds after monitor opens; USB reconnect can reset the board and hide early boot logs.
- Confirm monitor is attached to the same port shown by `pio device list`.
- Confirm baud rate is `115200` (matches `platformio.ini`).

For full logging behavior and examples, see [serial-monitor.md](serial-monitor.md).

## WiFi

### Device never connects

- Ensure `.env` has non-empty `WIFI_SSID` and `WIFI_PASS`, then rebuild/upload.
- Leave both empty only for intentional BLE-only builds (WiFi radio off).
- Use a 2.4 GHz network (ESP32-C3 setups commonly fail on 5 GHz-only networks).

## Bluetooth

### Mac repeatedly reports “Peer removed pairing information”

This is a stale bond left by older firmware. In **System Settings → Bluetooth**,
forget the blaster once, then reconnect. Current firmware uses encrypted
non-bonded sessions, so this mismatch cannot recur.

### Client connects but operations are unauthorized

- Set a random 16–64 character `BLE_AUTH_TOKEN` in firmware `.env` and reflash.
- Put the exact same value in the Mac client's `config.yaml` as `ble.auth_token`.

## Web UI

### Blank page, stale page, or missing assets

- Rebuild and upload LittleFS after changing `data/`:
  - `pio run --target buildfs`
  - `pio run --target uploadfs`
- On first install, run the full sequence:
  - `pio run --target upload`
  - `pio run --target buildfs`
  - `pio run --target uploadfs`

## IR receive/send

### No IR detected or no transmit response

- Re-check wiring against [wiring.md](wiring.md):
  - GPIO 10 for IR receive
  - GPIO 4 for IR send
- Confirm receiver and LED power/ground paths are correct.
  - Receiver on **3.3 V**; IR LED anode on **5 V** via the series resistor.
  - **Shared GND** between both rails and the ESP.
  - Transmit-only board: set `IR_RECV_ENABLED=0` in `.env` and rebuild so GPIO 10 is not polled.
- Confirm transistor and resistor placement for the IR LED driver.
- If you changed hardware pins, ensure firmware pin definitions match your wiring.
