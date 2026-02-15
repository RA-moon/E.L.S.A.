# 2026-02-15 ota-upload-env

## Context
- Configure OTA upload target for device at 192.168.31.154 with password auth.

## Changes
- Set OTA env upload port to 192.168.31.154.
- Add OTA auth flag using `OTA_PASSWORD` from the environment.

## Evidence
- platformio.ini updated.

## Commands
- None.

## Tests/Checks
- Not run (config-only change).

## Risks/Rollback
- None; USB env remains unchanged.

## Open questions
- None.

## Next steps
- Export `OTA_PASSWORD` in your shell and run `pio run -e esp32-s3-super-mini-ota -t upload`.
