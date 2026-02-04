# Run Log — 2026-02-04-brightness-telemetry

## Context (goal)
- Expose the configured brightness value in `/status` telemetry.

## Changes (what + why, file-level)
- src/main.cpp: include `brightness.value` in JSON status output.

## Evidence (logs/output/screens)
- No persistent logs or screenshots generated.

## Commands (state-changing only) + result
- None.

## Tests/Checks (run or reason skipped)
- Skipped (not requested).

## Risks/Rollback (1–3 lines)
- No functional changes outside telemetry. Rollback not applicable.

## Open questions (if any) + Next steps
- None.

## Diff tour (if applicable)
- Add brightness setting to `/status` payload.
