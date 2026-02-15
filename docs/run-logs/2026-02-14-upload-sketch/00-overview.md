# 2026-02-14 upload-sketch

## Context
- Upload firmware to ESP32-S3 over USB at /dev/cu.usbmodem21301.

## Changes
- No code or config changes.

## Evidence
- Upload succeeded via esptool (SUCCESS, 9.38s) on /dev/cu.usbmodem21301.

## Commands
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini -t upload --upload-port /dev/cu.usbmodem21301` -> success

## Tests/Checks
- `pio run -e esp32-s3-super-mini`: skipped (upload target already builds).

## Risks/Rollback
- If needed, reflash a previous known-good firmware image.

## Open questions
- None.

## Next steps
- Optionally run `pio device monitor -e esp32-s3-super-mini` to confirm WiFi IP and I2S init OK.
