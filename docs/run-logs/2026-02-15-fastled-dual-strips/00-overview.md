# 2026-02-15 fastled-dual-strips

## Context
- Hair strip works alone, but not when both strips are driven.
- Switch to FastLED to improve multi-strip reliability on ESP32-S3.

## Changes
- Replace Adafruit NeoPixel usage with FastLED `CRGB` buffers.
- Mirror main strip into hair strip using FastLED buffers.
- Update web `/frame` output and TEST_SOLID_COLOR path for FastLED.
- Swap PlatformIO dependency to FastLED.

## Evidence
- `pio run -e esp32-s3-super-mini` succeeded (FastLED build warning about `esp_memory_utils.h` noted).

## Commands
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini` -> success

## Tests/Checks
- `pio run -e esp32-s3-super-mini`

## Risks/Rollback
- If FastLED conflicts with other peripherals, revert to Adafruit NeoPixel and revisit dual-strip driver setup.

## Open questions
- None.

## Next steps
- Upload and verify both strips update simultaneously.
