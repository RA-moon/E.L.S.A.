# 2026-02-15 ota-hostname-fix

## Context
- OTA upload failed due to malformed hostname and mDNS lookup issues.

## Changes
- Sanitize `OTA_HOSTNAME` in the OTA upload script (strip trailing dots, accept `.local` or IPs).

## Evidence
- OTA upload still failed: host `E.L.S.A.local` not found.

## Commands
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-ota -t upload` -> failed (host not found)

## Tests/Checks
- Not run (upload attempt only).

## Risks/Rollback
- None.

## Open questions
- What is the device's current IP address?

## Next steps
- Set `OTA_IP` in `include/ota_secrets.h` and retry OTA upload.
