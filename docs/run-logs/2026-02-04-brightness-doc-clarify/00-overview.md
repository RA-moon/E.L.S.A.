# Run Log — 2026-02-04-brightness-doc-clarify

## Context (goal)
- Clarify that brightness sets the maximum output and decay is relative to it.

## Changes (what + why, file-level)
- src/main.cpp: update brightness envelope comment to note it’s relative to the brightness setting.
- README.md: clarify brightness behavior as a percentage of the brightness setting.

## Evidence (logs/output/screens)
- No persistent logs or screenshots generated.

## Commands (state-changing only) + result
- None.

## Tests/Checks (run or reason skipped)
- Skipped (doc/comment changes only).

## Risks/Rollback (1–3 lines)
- No functional changes. Rollback not applicable.

## Open questions (if any) + Next steps
- None.

## Diff tour (if applicable)
- Align documentation/comments with brightness behavior.
