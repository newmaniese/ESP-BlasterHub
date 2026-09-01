"""PlatformIO pre-script: load build flags from .env."""

Import("env")  # type: ignore  # PlatformIO injects this

import os
from pathlib import Path


def _load_dotenv(path: Path) -> dict:
    values = {}
    if not path.is_file():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def _as_bool01(raw, default="1"):
    text = (raw if raw is not None else default).strip().lower()
    if text in ("0", "false", "no", "off"):
        return 0
    if text in ("1", "true", "yes", "on"):
        return 1
    return 1 if default != "0" else 0


def _as_int(raw, default, min_v, max_v):
    try:
        value = int(str(raw if raw is not None else default).strip())
    except (TypeError, ValueError):
        value = default
    if value < min_v:
        return min_v
    if value > max_v:
        return max_v
    return value


project_dir = Path(env["PROJECT_DIR"])  # type: ignore[name-defined]
dotenv = _load_dotenv(project_dir / ".env")

ble_device_name = dotenv.get("BLE_DEVICE_NAME", "").strip()
if not ble_device_name:
    raise ValueError("BLE_DEVICE_NAME must be set in .env")

ble_auth_token = os.environ.get(
    "BLE_AUTH_TOKEN", dotenv.get("BLE_AUTH_TOKEN", "")
).strip()
if len(ble_auth_token) < 16:
    raise ValueError("BLE_AUTH_TOKEN must be at least 16 characters in .env")
if len(ble_auth_token) > 64:
    raise ValueError("BLE_AUTH_TOKEN must be at most 64 characters in .env")

ir_recv_enabled = _as_bool01(dotenv.get("IR_RECV_ENABLED", "1"))
ir_send_repeat = _as_int(dotenv.get("IR_SEND_REPEAT", "1"), default=1, min_v=1, max_v=20)

wifi_ssid = dotenv.get("WIFI_SSID", "").strip()
wifi_pass = dotenv.get("WIFI_PASS", "").strip()
if bool(wifi_ssid) != bool(wifi_pass):
    raise ValueError(
        "WIFI_SSID and WIFI_PASS must both be set, or both empty/omitted "
        "(empty = BLE-only, WiFi radio off)"
    )
if "WIFI_ENABLED" in dotenv:
    print(
        "[pio_env_flags] WARNING: WIFI_ENABLED is ignored; "
        "leave WIFI_SSID/WIFI_PASS empty for BLE-only"
    )
wifi_enabled = 1 if wifi_ssid and wifi_pass else 0

cppdefines = [
    ("BLE_DEVICE_NAME", env.StringifyMacro(ble_device_name)),  # type: ignore[name-defined]
    ("BLE_AUTH_TOKEN", env.StringifyMacro(ble_auth_token)),  # type: ignore[name-defined]
    ("IR_RECV_ENABLED", ir_recv_enabled),
    ("IR_SEND_REPEAT", ir_send_repeat),
    ("WIFI_ENABLED", wifi_enabled),
]
if wifi_enabled:
    cppdefines.append(("WIFI_SSID", env.StringifyMacro(wifi_ssid)))  # type: ignore[name-defined]
    cppdefines.append(("WIFI_PASS", env.StringifyMacro(wifi_pass)))  # type: ignore[name-defined]

env.Append(CPPDEFINES=cppdefines)  # type: ignore[name-defined]
print(
    f"[pio_env_flags] BLE_DEVICE_NAME={ble_device_name!r} "
    f"BLE_AUTH_TOKEN=<configured:{len(ble_auth_token)}> "
    f"IR_RECV_ENABLED={ir_recv_enabled} IR_SEND_REPEAT={ir_send_repeat} "
    f"WIFI_ENABLED={wifi_enabled}"
    + (f" WIFI_SSID={wifi_ssid!r}" if wifi_enabled else " (no WIFI_SSID/WIFI_PASS)")
)
