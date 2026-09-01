# ESP32-C3 IR Blaster — PlatformIO wrappers
# Requires: pio (PlatformIO CLI) on PATH

.PHONY: build upload fs monitor test sync-mac-client help

# Full install: firmware + LittleFS frontend
build:
	pio run --target upload
	pio run --target buildfs
	pio run --target uploadfs

# Firmware only
upload:
	pio run --target upload

# Frontend filesystem only (after data/ changes)
fs:
	pio run --target buildfs
	pio run --target uploadfs

monitor:
	pio device monitor

test:
	pio test -e esp32c3-test

sync-mac-client:
	python3 scripts/sync_mac_client.py

help:
	@echo "make build            - upload firmware + build/upload filesystem"
	@echo "make upload           - firmware only"
	@echo "make fs               - LittleFS only (data/)"
	@echo "make monitor          - serial monitor"
	@echo "make test             - on-device unit tests"
	@echo "make sync-mac-client  - copy BLE name/token from .env to the Mac client"
