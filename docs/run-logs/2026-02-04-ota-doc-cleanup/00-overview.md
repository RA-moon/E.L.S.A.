# Run Log — 2026-02-04-ota-doc-cleanup

## Context (goal)
- Fix OTA/Wi-Fi gating, reconcile docs with code, and remove unused FFT/legacy helpers.

## Changes (what + why, file-level)
- src/main.cpp: initialize Wi-Fi when OTA or telemetry is enabled; updated brightness envelope comments.
- README.md: align speed mapping, brightness envelope, and wave spacing limits with current code.
- include/frame_interpolation.h, src/frame_interpolation.cpp: remove unused interpolated-frame API.
- include/audio_processor.h, src/audio_processor.cpp: remove unused brightness pulse plumbing.
- include/arduinoFFT.h, src/arduinoFFT.cpp: remove unused legacy FFT sources.

## Evidence (logs/output/screens)
- No persistent logs or screenshots generated.

## Commands (state-changing only) + result
- None.

## Tests/Checks (run or reason skipped)
- Skipped (not requested).

## Risks/Rollback (1–3 lines)
- OTA now depends on Wi-Fi setup even when telemetry is off; if issues arise, revert the Wi-Fi gating change in `src/main.cpp`.
- Removing unused FFT/helpers could affect any external references; restore deleted files if needed.

## Open questions (if any) + Next steps
- None.

## Diff tour (if applicable)
- Update Wi-Fi initialization flow so OTA doesn’t depend on telemetry.
- Bring README’s speed/brightness/spacing docs in sync with firmware behavior.
- Remove unused frame interpolation and legacy FFT code.
