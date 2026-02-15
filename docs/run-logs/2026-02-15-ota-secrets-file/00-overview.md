# 2026-02-15 ota-secrets-file

## Context
- Use `include/ota_secrets.h` to supply OTA auth automatically and select the OTA target.

## Changes
- Add `scripts/ota_from_secrets.py` to read `OTA_PASSWORD` and append `--auth=...` for OTA uploads.
- Read `OTA_IP` (if set) or `OTA_HOSTNAME` to set the OTA upload target automatically.
- Wire the OTA env to the new script and remove the env-var based `upload_flags`.
- Update README OTA instructions and example secrets placeholders.

## Evidence
- Files updated: `scripts/ota_from_secrets.py`, `platformio.ini`, `README.md`, `include/ota_secrets.example.h`.

## Commands
- None.

## Tests/Checks
- Not run (config/script-only change).

## Risks/Rollback
- If the script fails to parse the password, OTA uploads will proceed without auth and fail if the device requires it.

## Open questions
- None.

## Next steps
- Copy `include/ota_secrets.example.h` to `include/ota_secrets.h` and fill in credentials and optional `OTA_IP`.
