# Run Log — 2026-02-04-pulse-lead

## Context (goal)
- Add a configurable pulse lead to align peak brightness with the perceived beat.

## Changes (what + why, file-level)
- src/main.cpp: add `pulseLeadMs` to runtime config, clamp to +/-250 ms, expose via `/config`, and apply it to the pulse calculation.
- README.md: document `pulseLeadMs` and update pulse formula.

## Evidence (logs/output/screens)
- No persistent logs or screenshots generated.

## Commands (state-changing only) + result
- None.

## Tests/Checks (run or reason skipped)
- Skipped (not requested).

## Risks/Rollback (1–3 lines)
- Large lead values could make pulses feel early or muted; clamp can be adjusted if needed.

## Open questions (if any) + Next steps
- None.

## Diff tour (if applicable)
- Add pulse lead offset and document behavior.
