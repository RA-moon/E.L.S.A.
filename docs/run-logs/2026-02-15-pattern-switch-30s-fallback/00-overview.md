# 2026-02-15 pattern-switch-30s-fallback

## Context
- Animations still not switching; enforce a 30s max interval regardless of BPM changes.

## Changes
- Make the 30s auto-switch timer unconditional in auto mode (resets on any switch).

## Evidence
- `pio run -e esp32-s3-super-mini` succeeded.

## Commands
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini` -> success

## Tests/Checks
- `pio run -e esp32-s3-super-mini`

## Risks/Rollback
- If you want BPM-only switching, restore the previous BPM-availability guard.

## Open questions
- None.

## Next steps
- Upload and confirm patterns switch at least every 30 seconds.
