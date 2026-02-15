# 2026-02-14 hair-strip-mirror

## Context
- Hair strip not lighting; enable actual rendering for the second strip.

## Changes
- Mirror strip1 pixels onto strip2 each frame so the hair strip displays the same animation.
- Extend TEST_SOLID_COLOR mode to drive strip2 for wiring verification.

## Evidence
- `pio run -e esp32-s3-super-mini` succeeded.

## Commands
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini` -> success

## Tests/Checks
- `pio run -e esp32-s3-super-mini`

## Risks/Rollback
- If hair animation looks incorrect, revert to previous behavior by removing the mirror helper and call.

## Open questions
- None.

## Next steps
- Upload and verify hair strip output on hardware.
