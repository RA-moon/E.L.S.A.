# Run Log — 2026-02-04-pulse-cleanup

## Context (goal)
- Reintroduce ease-out beat pulse as a post-render brightness multiplier, keep idle at 70%, tighten beat range clamps, remove redundant fields, and clean documentation.

## Changes (what + why, file-level)
- src/main.cpp: add pulse envelope helper, apply pulse after rendering, simplify base brightness logic, clamp avg beat window to 430..800 in /config, expose base/pulse ratios in telemetry, and update comments.
- include/wave_position.h, src/wave_position.cpp: remove unused `totalWidth` field.
- README.md: update brightness/pulse formulas, add audio default note, and replace non-ASCII characters with ASCII.

## Evidence (logs/output/screens)
- No persistent logs or screenshots generated.

## Commands (state-changing only) + result
- None.

## Tests/Checks (run or reason skipped)
- Skipped (not requested).

## Risks/Rollback (1–3 lines)
- Pulse now scales final LED colors; if it over-dims, revert to the prior brightness-only envelope in `src/main.cpp`.
- Removing `totalWidth` is safe unless external code was depending on it.

## Open questions (if any) + Next steps
- None.

## Diff tour (if applicable)
- Add post-render pulse multiplier, optimize scaling, and expose pulse/base ratios in telemetry.
- Tighten /config beat range clamping to 430..800.
- Clean README and align formulas with code.
