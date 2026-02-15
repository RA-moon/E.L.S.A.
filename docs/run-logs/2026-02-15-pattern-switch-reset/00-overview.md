# 2026-02-15 pattern-switch-reset

## Context
- Reset auto-switch timer whenever animations change, including manual (button) switches.

## Changes
- Reset `lastSwitchTime` and `s_lastSwitchBpm` when applying a manual (fixed) animation.

## Evidence
- `pio run -e esp32-s3-super-mini` succeeded.

## Commands
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini` -> success

## Tests/Checks
- `pio run -e esp32-s3-super-mini`

## Risks/Rollback
- None; only affects auto-switch timing.

## Open questions
- None.

## Next steps
- Upload and confirm auto-switching behaves as expected.
