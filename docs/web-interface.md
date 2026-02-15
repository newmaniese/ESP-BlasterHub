# ESP32-C3 IR Blaster – Web Interface & Software

This document describes the web UI, stored codes, HTTP API, and WebSocket usage. For hardware, see [wiring.md](wiring.md).

---

## Architecture

The frontend is a set of **static files** served from **LittleFS** (the ESP32 flash filesystem):

| File | Purpose |
|------|---------|
| `data/index.html` | Page structure. Uses `%PLACEHOLDER%` tokens replaced at serve-time by the ESP32 template processor. |
| `data/app.css` | All styles — mobile-first responsive layout, button sizes, log colors. |
| `data/app.js` | All behavior — WebSocket, saved-list rendering, send/save/rename/delete, activity log. |

The server (`ESPAsyncWebServer`) serves `index.html` through its built-in template processor, which replaces:

- `%DEVICE_IP%` — the device's current WiFi IP address.
- `%INITIAL_SAVED_COUNT%` — number of saved codes at page-load time.

Static assets (`app.css`, `app.js`) are served directly from LittleFS with `Cache-Control: max-age=86400`.

All dynamic data (saved codes, live IR events, send commands) flows through **JSON APIs** and **WebSocket** — the HTML itself is fully static aside from the two small boot-time placeholders above.

### Deployment

After changing frontend files, rebuild and upload the filesystem image:

```bash
pio run --target buildfs
pio run --target uploadfs
```

Firmware (`src/main.cpp`) changes require the normal build/upload cycle.

---

## WebSocket (real-time updates and send)

The device exposes a WebSocket at **`ws://<device-ip>/ws`** on the same port as HTTP (80). Use it for live "Last received" updates and for sending IR commands without polling.

- **Server → client (IR event):** When the IR receiver decodes a code, the server pushes a JSON message to all connected clients:
  - `event`: `"ir"`
  - `seq`: sequence number
  - `human`: human-readable decode
  - `raw`: raw/source-style string
  - `replayUrl`: e.g. `/send?type=nec&data=...&length=32` (empty if not sendable)
  - `protocol`: e.g. `"NEC"`
  - `value`: hex string, e.g. `"FF827D00"`
  - `bits`: e.g. `32`
- **Client → server (send command):** Send a JSON message: `{ "cmd": "send", "type": "nec", "data": "<hex>", "length": 32, "name": "<optional name>" }`. The device sends the NEC code and replies with e.g. `{ "ok": true, "msg": "Sent NEC ...", "name": "<name>" }`. The UI uses this for stored-code **Send** when the WebSocket is open, with HTTP `GET /send?...` as fallback when disconnected.
- **On connect:** The server sends the current "last received" state (same JSON shape as an IR event, including `protocol`, `value`, `bits` when available) so a newly opened page is up to date.

All existing HTTP endpoints (e.g. `/send`, `/save`, `/saved`) remain valid for scripts, bookmarks, and the manual form.

---

## Main page (`GET /`)

The root page is a responsive, mobile-first HTML document. No page refresh is needed to see new IR codes: the "Last received" section updates in real time over WebSocket.

### Layout

On **mobile** (single column, top to bottom):

1. **Header** — device IP, WebSocket status badge (Live / Disconnected), links to JSON and Dump.
2. **Stored Commands** — saved codes with large Send buttons (≥44px touch target), Edit, and Del.
3. **Last Received** — live IR decode with Replay and Save.
4. **Store a Code** — manual entry form.
5. **Activity Log** — real-time log of IR receives and sends.

On **tablet / desktop** (≥640px) — two-column grid:

| Left column | Right column |
|-------------|-------------|
| Last Received | Stored Commands (spans full height) |
| Store a Code | |
| Activity Log | |

### Stored Commands

- List of all codes saved in NVS. Each row has:
  - **Name** (or "Code *i*" if unnamed).
  - **Protocol / value / bits** shown as secondary metadata.
  - **Send** — large green button. Sends over WebSocket when connected, HTTP fallback otherwise. A modal confirms "Sent: *name*", then closes after ~1.8 s.
  - **Edit** — prompts for a new name and renames the saved item.
  - **Del** — asks for confirmation, then deletes. The list refreshes in place.
- If no stored codes exist, shows: "None yet. Save codes from the form or from received signals."

### Last Received

- Human-readable and raw (behind a `<details>` toggle) view of the most recent IR decode.
- Updates automatically when a new code arrives over WebSocket. A green pulsing dot indicates live connection.
- **Replay** — sends the last code (NEC only).
- **Save** — saves the last code with an optional name (stays on page).

### Store a Code (manual form)

- **Name**, **Protocol** (NEC), **Value** (hex), **Bits** (default 32).
- Submit saves in the background with `fetch()` (no page navigation).

### Activity Log

A scrollable, capped (50 entries) log of IR activity. Each entry shows a timestamp and description. Entries are **color-coded** by matching against stored commands:

| Class | Meaning | Color |
|-------|---------|-------|
| `log-known-exact` | Received signal matches a stored code (protocol + value + bits) | Green |
| `log-known-likely` | Protocol + value match but bits differ | Amber |
| `log-unknown` | No matching stored code | Grey |
| `log-send` | Outgoing send attempt or acknowledgment | Blue |
| `log-failed` | Send failure | Red |

The matching works by maintaining a local JavaScript index of saved commands (refreshed from `/saved`) and comparing each incoming IR event's `protocol`, `value`, and `bits` fields.

### WebSocket status

A badge in the header shows connection state:
- **Live** (green) — WebSocket connected.
- **Disconnected** (red) — WebSocket down; auto-reconnects every 2 s.

### Modal on send

When you click **Send**, on success a centered modal appears with **"Sent:"** and the command name, then hides automatically. The page stays on the same view.

---

## Stored codes (persistence)

- Saved codes are stored in **NVS** (Preferences), namespace `ir_saved`. They survive reboots.
- Each entry stores: **name**, **protocol**, **value** (hex), **bits**.
- **Save** sources:
  - Manual form (name, protocol, value, bits).
  - "Save" next to **Last received** (optional name).
- **Dump** (`GET /dump`) returns plain text: C-style comments and `irsend.sendNEC(...)` lines for pasting into firmware. Non-NEC codes are listed as comments with value and name.

---

## HTTP API

All endpoints remain valid. WebSocket is optional; the UI uses it for live updates and for Send when connected.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/` | Main page (HTML from LittleFS with template processor). |
| `GET` | `/app.css` | Stylesheet (static, from LittleFS). |
| `GET` | `/app.js` | JavaScript (static, from LittleFS). |
| `GET` | `/ip` | Plain text device IP. |
| `GET` | `/last` | JSON: `{ "seq", "human", "raw", "replayUrl" }` (fallback for scripts; live updates use WebSocket). |
| `GET` | `/send?type=nec&data=HEX&length=32&repeat=1` | Send NEC code (hex data, bit length, optional repeat). |
| `GET` | `/save?name=...` | Save the **last received** code with optional name. |
| `GET` | `/save?protocol=...&value=HEX&length=...&name=...` | Save a specific code by parameters. |
| `POST` | `/save` | Save from JSON body: `{ "name", "protocol", "value", "bits" }`. |
| `GET` | `/saved` | JSON array of all saved codes (index, name, protocol, value, bits). |
| `POST` | `/saved/delete?index=N` | Delete saved code at index `N`; shifts remaining. Returns `{ "ok", "remaining" }`. |
| `POST` | `/saved/rename?index=N&name=NewName` | Rename saved code at index `N`. Returns `{ "ok", "index" }`. |
| `GET` | `/dump` | Plain text dump for hardcoding (comments + NEC send lines). |

---

## Software overview

- **Firmware:** `src/main.cpp` — WiFi, LittleFS, AsyncWebServer, WebSocket, IR recv/send, NVS stored codes, template processor.
- **Frontend:** `data/index.html`, `data/app.css`, `data/app.js` — static files in LittleFS. Template tokens (`%DEVICE_IP%`, `%INITIAL_SAVED_COUNT%`) replaced at serve-time.
- **Stack:** Arduino framework, WiFi (STA), **ESPAsyncWebServer** + **AsyncWebSocket** on port 80, **LittleFS** for static files, **Preferences** (NVS) for saved codes, **ArduinoJson**, **IRremoteESP8266** (IRrecv on GPIO 10, IRsend on GPIO 4).
- **IR:** Receive buffer and timeout tuned for typical remotes; last code and short history in RAM. Only **NEC** is sent; other protocols can be stored and dumped.

---

## Quick reference – URLs

- Main UI: `http://<device-ip>/`
- WebSocket: `ws://<device-ip>/ws`
- IP only: `http://<device-ip>/ip`
- Send NEC: `http://<device-ip>/send?type=nec&data=FF827D&length=32`
- List saved (JSON): `http://<device-ip>/saved`
- Dump for code: `http://<device-ip>/dump`
