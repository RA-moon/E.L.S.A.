# 2026-02-15 usb-upload

## Context
- Upload firmware over USB because OTA was blocked by network isolation.

## Changes
- None (upload only).

## Evidence
- USB upload succeeded on /dev/cu.usbmodem21101.

## Commands
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini -t upload --upload-port /dev/cu.usbmodem21101` -> success

## Tests/Checks
- Not run (upload target builds the firmware as part of upload).

## Risks/Rollback
- None.

## Open questions
- None.

## Next steps
- Optionally run `pio device monitor -e esp32-s3-super-mini` to confirm WiFi IP/OTA ready.
