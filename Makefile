# ESP32-C3 IR Blaster — PlatformIO wrappers
# Requires: pio (PlatformIO CLI) on PATH

.PHONY: build upload fs monitor test help

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

help:
	@echo "make build    - upload firmware + build/upload filesystem"
	@echo "make upload   - firmware only"
	@echo "make fs       - LittleFS only (data/)"
	@echo "make monitor  - serial monitor"
	@echo "make test     - on-device unit tests"
