"""
Integration tests for the ESP32-C3 IR Blaster HTTP API.

These tests hit a real device on the network.  Set the DEVICE_IP environment
variable to the base URL before running, e.g.:

    DEVICE_IP=http://192.168.1.42 pytest test/integration/

Dependencies: pytest, requests  (see requirements-test.txt)
"""

import os
import re

import pytest
import requests

BASE_URL = os.environ.get("DEVICE_IP", "http://192.168.1.100")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def url(path: str) -> str:
    return BASE_URL.rstrip("/") + path


# ---------------------------------------------------------------------------
# GET /  (main page)
# ---------------------------------------------------------------------------

class TestRoot:
    def test_status_200(self):
        r = requests.get(url("/"))
        assert r.status_code == 200

    def test_contains_title(self):
        r = requests.get(url("/"))
        assert "IR Blaster" in r.text

    def test_contains_last_received(self):
        r = requests.get(url("/"))
        assert "Last received" in r.text

    def test_content_type_html(self):
        r = requests.get(url("/"))
        assert "text/html" in r.headers.get("Content-Type", "")


# ---------------------------------------------------------------------------
# GET /ip
# ---------------------------------------------------------------------------

class TestIp:
    def test_status_200(self):
        r = requests.get(url("/ip"))
        assert r.status_code == 200

    def test_looks_like_ipv4(self):
        r = requests.get(url("/ip"))
        assert re.match(r"\d+\.\d+\.\d+\.\d+", r.text.strip())


# ---------------------------------------------------------------------------
# GET /last
# ---------------------------------------------------------------------------

class TestLast:
    def test_status_200(self):
        r = requests.get(url("/last"))
        assert r.status_code == 200

    def test_json_keys(self):
        r = requests.get(url("/last"))
        data = r.json()
        for key in ("seq", "human", "raw", "replayUrl"):
            assert key in data, f"Missing key: {key}"

    def test_seq_is_int(self):
        r = requests.get(url("/last"))
        data = r.json()
        assert isinstance(data["seq"], int)


# ---------------------------------------------------------------------------
# GET /send
# ---------------------------------------------------------------------------

class TestSend:
    def test_send_nec_success(self):
        r = requests.get(url("/send"), params={
            "type": "nec",
            "data": "FF827D",
            "length": 32,
        })
        assert r.status_code == 200
        assert "Sent NEC" in r.text

    def test_send_missing_params(self):
        r = requests.get(url("/send"))
        assert r.status_code == 400

    def test_send_unsupported_type(self):
        r = requests.get(url("/send"), params={
            "type": "rc5",
            "data": "1234",
        })
        assert r.status_code == 400
        assert "Unsupported" in r.text


# ---------------------------------------------------------------------------
# GET /saved
# ---------------------------------------------------------------------------

class TestSaved:
    def test_status_200(self):
        r = requests.get(url("/saved"))
        assert r.status_code == 200

    def test_returns_json_array(self):
        r = requests.get(url("/saved"))
        data = r.json()
        assert isinstance(data, list)


# ---------------------------------------------------------------------------
# POST /save  (JSON body)  +  POST /saved/delete  (cleanup)
# ---------------------------------------------------------------------------

class TestSaveAndDelete:
    """Save a code, verify it appears in /saved, then delete it."""

    def test_save_and_delete_roundtrip(self):
        # 1) Save
        payload = {
            "name": "_test_code_",
            "protocol": "NEC",
            "value": "DEADBEEF",
            "bits": 32,
        }
        r = requests.post(url("/save"), json=payload)
        assert r.status_code == 200
        body = r.json()
        assert body.get("ok") is True
        saved_index = body["index"]

        # 2) Verify it shows up in /saved
        r2 = requests.get(url("/saved"))
        items = r2.json()
        names = [it.get("name") for it in items]
        assert "_test_code_" in names

        # 3) Delete
        r3 = requests.post(url("/saved/delete"), params={"index": saved_index})
        assert r3.status_code == 200
        assert r3.json().get("ok") is True

    def test_save_missing_value_returns_400(self):
        payload = {"name": "bad", "protocol": "NEC"}
        r = requests.post(url("/save"), json=payload)
        assert r.status_code == 400

    def test_save_invalid_json_returns_400(self):
        r = requests.post(
            url("/save"),
            data="this is not json",
            headers={"Content-Type": "application/json"},
        )
        assert r.status_code == 400


# ---------------------------------------------------------------------------
# GET /save  (query-string save with explicit params)
# ---------------------------------------------------------------------------

class TestSaveGet:
    def test_save_via_query(self):
        r = requests.get(url("/save"), params={
            "protocol": "NEC",
            "value": "CAFE",
            "length": 32,
            "name": "_qtest_",
        })
        assert r.status_code == 200
        body = r.json()
        assert body.get("ok") is True
        # Cleanup
        requests.post(url("/saved/delete"), params={"index": body["index"]})


# ---------------------------------------------------------------------------
# GET /dump
# ---------------------------------------------------------------------------

class TestDump:
    def test_status_200(self):
        r = requests.get(url("/dump"))
        assert r.status_code == 200

    def test_plain_text(self):
        r = requests.get(url("/dump"))
        assert "text/plain" in r.headers.get("Content-Type", "")

    def test_contains_header(self):
        r = requests.get(url("/dump"))
        assert "Saved IR codes" in r.text


# ---------------------------------------------------------------------------
# 404
# ---------------------------------------------------------------------------

class TestNotFound:
    def test_unknown_path_returns_404(self):
        r = requests.get(url("/nonexistent"))
        assert r.status_code == 404
