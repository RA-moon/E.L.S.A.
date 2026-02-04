# Run Log — 2026-02-04-wifi-keepalive-cleanup

## Context (goal)
- Apply requested cleanup: align I2S pin flags, remove unused beat helper, reduce debug noise, add optional Wi-Fi keepalive (disabled by default).

## Changes (what + why, file-level)
- platformio.ini: switch build flags to `I2S_*` macros used by the audio driver.
- src/main.cpp: remove unused `beatEnvelope`, disable wave timing debug by default, add optional Wi-Fi keepalive/reconnect, and make WiFiMulti persistent for reuse.

## Evidence (logs/output/screens)
- No persistent logs or screenshots generated.

## Commands (state-changing only) + result
- None.

## Tests/Checks (run or reason skipped)
- Skipped (not requested).

## Risks/Rollback (1–3 lines)
- Wi-Fi keepalive is disabled by default; enable with `ENABLE_WIFI_KEEPALIVE` if needed.
- Revert `platformio.ini` build flag changes if the previous macro names were intended for a different driver.

## Open questions (if any) + Next steps
- None.

## Diff tour (if applicable)
- Align platform flags to I2S macros used in firmware.
- Add optional Wi-Fi keepalive logic (disabled).
- Remove unused beat envelope helper and silence wave timing debug.
