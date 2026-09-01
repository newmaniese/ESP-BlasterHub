#!/usr/bin/env python3
"""Push BLE_DEVICE_NAME and BLE_AUTH_TOKEN from this repo's .env to the local
Blaster Mac Client (localhost UI, or the installed config.yaml).

Usage (from the irproject root or anywhere):
    python3 scripts/sync_mac_client.py
    make sync-mac-client
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ENV = REPO_ROOT / ".env"
DEFAULT_UI = "http://127.0.0.1:8765"
INSTALL_CONFIG = (
    Path.home()
    / "Library"
    / "Application Support"
    / "blaster-mac-client"
    / "config.yaml"
)
TOKEN_MIN, TOKEN_MAX = 16, 64


def _load_dotenv(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.is_file():
        raise FileNotFoundError(f"Missing {path} (copy .env.example to .env)")
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def _ble_from_env(env: dict[str, str]) -> tuple[str, str]:
    name = env.get("BLE_DEVICE_NAME", "").strip()
    token = env.get("BLE_AUTH_TOKEN", "").strip()
    if not name:
        raise ValueError("BLE_DEVICE_NAME is empty in .env")
    if not TOKEN_MIN <= len(token) <= TOKEN_MAX:
        raise ValueError(
            f"BLE_AUTH_TOKEN must be {TOKEN_MIN}–{TOKEN_MAX} characters in .env"
        )
    return name, token


def _yaml_scalar(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def _upsert_ble_yaml(text: str, device_name: str, auth_token: str) -> str:
    """Replace or insert ble.device_name and ble.auth_token without PyYAML."""
    if not re.search(r"(?m)^ble:\s*$", text):
        block = (
            f"ble:\n  device_name: {_yaml_scalar(device_name)}\n"
            f"  auth_token: {_yaml_scalar(auth_token)}\n"
        )
        return block + text

    lines = text.splitlines(keepends=True)
    in_ble = False
    saw_name = saw_token = False
    out: list[str] = []
    ble_indent = "  "

    def flush_missing() -> None:
        if not saw_name:
            out.append(f"{ble_indent}device_name: {_yaml_scalar(device_name)}\n")
        if not saw_token:
            out.append(f"{ble_indent}auth_token: {_yaml_scalar(auth_token)}\n")

    for line in lines:
        stripped = line.strip()
        if re.match(r"^ble:\s*(#.*)?$", stripped):
            if in_ble:
                flush_missing()
            in_ble = True
            saw_name = saw_token = False
            out.append(line)
            continue
        if in_ble and stripped and not line.startswith((" ", "\t")) and not stripped.startswith("#"):
            flush_missing()
            in_ble = False
        if in_ble:
            m = re.match(r"^(\s*)(device_name|auth_token):\s*.*", line)
            if m:
                ble_indent = m.group(1) or "  "
                key = m.group(2)
                value = device_name if key == "device_name" else auth_token
                comment = ""
                hash_at = line.find(" #")
                if hash_at != -1:
                    comment = line[hash_at:].rstrip("\n")
                nl = "\n" if line.endswith("\n") else ""
                out.append(f"{ble_indent}{key}: {_yaml_scalar(value)}{comment}{nl}")
                if key == "device_name":
                    saw_name = True
                else:
                    saw_token = True
                continue
        out.append(line)

    if in_ble:
        flush_missing()
    return "".join(out)


def push_via_api(base_url: str, device_name: str, auth_token: str) -> dict:
    cfg_url = base_url.rstrip("/") + "/api/config"
    with urllib.request.urlopen(cfg_url, timeout=5) as resp:
        config = json.loads(resp.read().decode("utf-8"))
    ble = config.setdefault("ble", {})
    ble["device_name"] = device_name
    ble["auth_token"] = auth_token
    body = json.dumps(config).encode("utf-8")
    req = urllib.request.Request(
        cfg_url,
        data=body,
        method="PUT",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read().decode("utf-8"))


def write_install_config(path: Path, device_name: str, auth_token: str) -> None:
    if path.is_file():
        text = path.read_text(encoding="utf-8")
    else:
        text = "ble:\n"
        path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(_upsert_ble_yaml(text, device_name, auth_token), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Copy BLE_DEVICE_NAME and BLE_AUTH_TOKEN from .env to the Mac client."
    )
    parser.add_argument(
        "--env",
        type=Path,
        default=DEFAULT_ENV,
        help=f"path to firmware .env (default: {DEFAULT_ENV})",
    )
    parser.add_argument(
        "--ui",
        default=DEFAULT_UI,
        help=f"Mac client UI base URL (default: {DEFAULT_UI})",
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=INSTALL_CONFIG,
        help="installed config.yaml used if the UI is not reachable",
    )
    args = parser.parse_args(argv)

    try:
        name, token = _ble_from_env(_load_dotenv(args.env))
    except (OSError, ValueError) as e:
        print(e, file=sys.stderr)
        return 1

    print(f"From {args.env}: BLE_DEVICE_NAME={name!r} BLE_AUTH_TOKEN=<{len(token)} chars>")

    try:
        result = push_via_api(args.ui, name, token)
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, json.JSONDecodeError) as e:
        print(f"Mac client UI not reachable at {args.ui} ({e})")
        try:
            write_install_config(args.config, name, token)
        except OSError as write_err:
            print(write_err, file=sys.stderr)
            return 1
        print(f"Wrote {args.config}")
        print("Start the Mac client (or re-run this script while it is running) to apply.")
        return 0

    if not result.get("ok"):
        print(result, file=sys.stderr)
        return 1
    print(f"Updated running Mac client at {args.ui}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
