# 2026-02-15 pattern-switch-timer

## Context
- Adjust auto switching: 30s interval or switch when BPM changes (original behavior).

## Changes
- Set auto fallback interval to 30s and limit it to when BPM is unavailable.

## Evidence
- `pio run -e esp32-s3-super-mini` succeeded (FastLED warning about `esp_memory_utils.h` noted).

## Commands
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini` -> success

## Tests/Checks
- `pio run -e esp32-s3-super-mini`

## Risks/Rollback
- If you want time-based switching regardless of BPM, remove the BPM-availability guard.

## Open questions
- None.

## Next steps
- Upload and confirm 30s fallback or BPM-change switching.
