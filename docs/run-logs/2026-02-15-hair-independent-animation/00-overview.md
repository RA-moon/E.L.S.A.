# 2026-02-15 hair-independent-animation

## Context
- Hair strip should run its own animation (no beat reaction) and avoid blocking delays.

## Changes
- Replace hair mirroring with an independent, non-blocking hair animation update.
- Remove beat pulse scaling from the hair strip.

## Evidence
- `pio run -e esp32-s3-super-mini` succeeded.

## Commands
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini` -> success

## Tests/Checks
- `pio run -e esp32-s3-super-mini`

## Risks/Rollback
- If hair timing feels off, adjust `HAIR_UPDATE_MS` or revert to previous mirroring behavior.

## Open questions
- None.

## Next steps
- Upload and verify hair animation runs independently from the beat-driven brain strip.
