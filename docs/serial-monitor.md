# Serial monitor and logging

The ESP32-C3 IR Blaster uses **`printf()`** for all application logging. On this board, `Serial.print()` and ESP-IDF log macros may not appear in the serial monitor; `printf()` does.

## Viewing logs

1. **Upload** the firmware: `pio run --target upload`
2. **Open the monitor:** `pio device monitor`
3. After the device connects (or resets), wait a few seconds. You should see:
   - `[IR] --- ESP32-C3 IR Blaster boot ---`
   - `[IR] Connecting to Wi-Fi` then `[IR] IP: x.x.x.x` (or a timeout message)
   - `[IR] HTTP IR server started`
   - Every second: `[IR] IP: x.x.x.x` or `[IR] (WiFi not connected)`
   - When you load the web page: `[IR] Root page requested`
   - When IR is received: `[IR]` plus the decoded line and raw data

## If you see nothing

- **Reset timing:** When you open the monitor, the USB connection can reset the board. Boot output is often lost; the **heartbeat** (IP every second) will appear once the connection is stable. Wait at least 5–10 seconds.
- **Confirm port:** Use `pio device list` and ensure the monitor is connected to the same port as the ESP32-C3 (e.g. `/dev/cu.usbmodem1101` on macOS).

## Configuration

- **Baud rate:** 115200 (set in `platformio.ini` and in code).
- **Monitor filter:** `monitor_filters = direct` in `platformio.ini` so no filter hides output. You can also run `pio device monitor --filter direct` if needed.

## Why printf?

On this board, the serial monitor shows output from the same stream that `printf()` uses. `Serial.print()` and `ESP_LOG*` can go to a different backend or be filtered, so they may not appear. Using `printf("[IR] ...\n")` ensures logs are visible in the monitor.
