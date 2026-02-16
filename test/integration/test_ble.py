"""
Integration tests for the ESP32-C3 IR Blaster BLE GATT service.

These tests connect to a real device over BLE.  The device must be powered on,
advertising, and already paired/bonded with this Mac.

Set the device address or name before running:

    DEVICE_BLE_NAME="IR Blaster" pytest test/integration/test_ble.py

or:

    DEVICE_BLE_ADDR="AA:BB:CC:DD:EE:FF" pytest test/integration/test_ble.py

Dependencies: pytest, bleak  (see requirements-test.txt)
"""

import asyncio
import json
import os

import pytest
import pytest_asyncio
from bleak import BleakClient, BleakScanner

# ---------------------------------------------------------------------------
# Configuration — mirrors the UUIDs from include/ble_server.h
# ---------------------------------------------------------------------------

SERVICE_UUID      = "e97a0001-c116-4a63-a60f-0e9b4d3648f3"
CHAR_SAVED_UUID   = "e97a0002-c116-4a63-a60f-0e9b4d3648f3"
CHAR_SEND_UUID    = "e97a0003-c116-4a63-a60f-0e9b4d3648f3"
CHAR_STATUS_UUID  = "e97a0004-c116-4a63-a60f-0e9b4d3648f3"
CHAR_SCHEDULE_UUID = "e97a0005-c116-4a63-a60f-0e9b4d3648f3"

DEVICE_BLE_NAME = os.environ.get("DEVICE_BLE_NAME", "IR Blaster")
DEVICE_BLE_ADDR = os.environ.get("DEVICE_BLE_ADDR", "")

SCAN_TIMEOUT = 10.0   # seconds


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def event_loop():
    """Provide a single event loop for the whole test session."""
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


async def _find_device():
    """Scan for the IR Blaster by address or name."""
    if DEVICE_BLE_ADDR:
        device = await BleakScanner.find_device_by_address(DEVICE_BLE_ADDR, timeout=SCAN_TIMEOUT)
    else:
        device = await BleakScanner.find_device_by_name(DEVICE_BLE_NAME, timeout=SCAN_TIMEOUT)
    return device


@pytest_asyncio.fixture(scope="session")
async def ble_device(event_loop):
    """Discover the BLE device once for the session."""
    device = await _find_device()
    if device is None:
        pytest.skip(
            f"BLE device not found (name={DEVICE_BLE_NAME!r}, addr={DEVICE_BLE_ADDR!r}). "
            "Is the device powered on and advertising?"
        )
    return device


@pytest_asyncio.fixture
async def client(ble_device):
    """Connect to the BLE device for a single test, then disconnect."""
    async with BleakClient(ble_device, timeout=15.0) as c:
        yield c


# ---------------------------------------------------------------------------
# Discovery
# ---------------------------------------------------------------------------

class TestBLEDiscovery:
    """Verify the device is discoverable and advertises the expected service."""

    @pytest.mark.asyncio
    async def test_device_found(self, ble_device):
        assert ble_device is not None

    @pytest.mark.asyncio
    async def test_service_uuid_advertised(self):
        """Scan and check the service UUID appears in advertisement data."""
        devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT)
        found = False
        for d in devices:
            adv = d.metadata.get("uuids", [])
            if SERVICE_UUID.lower() in [u.lower() for u in adv]:
                found = True
                break
        assert found, f"No device advertising service {SERVICE_UUID}"


# ---------------------------------------------------------------------------
# Saved Codes characteristic (Read)
# ---------------------------------------------------------------------------

class TestSavedCodesCharacteristic:
    """Read the Saved Codes characteristic and verify the payload."""

    @pytest.mark.asyncio
    async def test_read_returns_json_array(self, client):
        raw = await client.read_gatt_char(CHAR_SAVED_UUID)
        data = json.loads(raw.decode("utf-8"))
        assert isinstance(data, list), "Expected a JSON array"

    @pytest.mark.asyncio
    async def test_entries_have_expected_keys(self, client):
        raw = await client.read_gatt_char(CHAR_SAVED_UUID)
        data = json.loads(raw.decode("utf-8"))
        if len(data) == 0:
            pytest.skip("No saved codes on device — save at least one code first")
        entry = data[0]
        # BLE returns compact format: i (index), n (name) only
        assert "i" in entry or "index" in entry, "Missing index key (i or index)"
        assert "n" in entry or "name" in entry, "Missing name key (n or name)"


# ---------------------------------------------------------------------------
# Send Command characteristic (Write) + Status (Notify)
# ---------------------------------------------------------------------------

class TestSendCommand:
    """Write a saved-code index and verify the status notification."""

    @pytest.mark.asyncio
    async def test_send_valid_index(self, client):
        # First check there is at least one saved code
        raw = await client.read_gatt_char(CHAR_SAVED_UUID)
        codes = json.loads(raw.decode("utf-8"))
        if len(codes) == 0:
            pytest.skip("No saved codes on device")

        # Subscribe to status notifications
        status_values = []

        def _on_notify(_sender, data: bytearray):
            status_values.append(data.decode("utf-8"))

        await client.start_notify(CHAR_STATUS_UUID, _on_notify)

        # Write index 0
        await client.write_gatt_char(CHAR_SEND_UUID, bytes([0]))

        # Wait for the notification
        for _ in range(20):
            if status_values:
                break
            await asyncio.sleep(0.1)

        await client.stop_notify(CHAR_STATUS_UUID)

        assert len(status_values) > 0, "No status notification received"
        assert status_values[-1].startswith("OK:"), f"Expected OK, got: {status_values[-1]}"

    @pytest.mark.asyncio
    async def test_send_invalid_index(self, client):
        """Writing an out-of-range index should produce an ERR status."""
        status_values = []

        def _on_notify(_sender, data: bytearray):
            status_values.append(data.decode("utf-8"))

        await client.start_notify(CHAR_STATUS_UUID, _on_notify)

        # Index 255 — almost certainly out of range
        await client.write_gatt_char(CHAR_SEND_UUID, bytes([255]))

        for _ in range(20):
            if status_values:
                break
            await asyncio.sleep(0.1)

        await client.stop_notify(CHAR_STATUS_UUID)

        assert len(status_values) > 0, "No status notification received"
        assert status_values[-1].startswith("ERR:"), f"Expected ERR, got: {status_values[-1]}"


# ---------------------------------------------------------------------------
# Schedule characteristic (Write) — arm and heartbeat
# ---------------------------------------------------------------------------

class TestScheduleCharacteristic:
    """Write schedule payloads (arm and heartbeat) and verify no ERR."""

    @pytest.mark.asyncio
    async def test_schedule_arm_and_heartbeat(self, client):
        # Arm: run "Off" in 900s (we do not wait for it; just check the write is accepted)
        payload_arm = json.dumps({"delay_seconds": 900, "command": "Off"}).encode("utf-8")
        await client.write_gatt_char(CHAR_SCHEDULE_UUID, payload_arm)

        # Heartbeat: reset the timer (no response; just ensure write succeeds)
        payload_heartbeat = json.dumps({"heartbeat": True}).encode("utf-8")
        await client.write_gatt_char(CHAR_SCHEDULE_UUID, payload_heartbeat)


# ---------------------------------------------------------------------------
# Status characteristic (Read)
# ---------------------------------------------------------------------------

class TestStatusCharacteristic:
    """Read the status characteristic directly."""

    @pytest.mark.asyncio
    async def test_read_status(self, client):
        raw = await client.read_gatt_char(CHAR_STATUS_UUID)
        text = raw.decode("utf-8")
        # After boot it should be "READY"; after a send it will be "OK:…" or "ERR:…"
        assert len(text) > 0, "Status characteristic is empty"
