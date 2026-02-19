## Context
User requested table overrides to be relative to runtime config values, including trigger behavior (`1.0` full, `0.0` off, `0.5` half).

## Changes
- Updated resolved effects to expose trigger scales (`beatWaveTriggerScale`, `fallbackWaveTriggerScale`) instead of treating trigger scale as boolean threshold only.
- Updated effect resolution to:
  - treat trigger scales as active when `> 0.0`
  - keep all other scale fields relative to runtime config values.
- Added deterministic fractional trigger consumption in animation engine so trigger scales apply proportionally:
  - `1.0`: trigger every eligible event
  - `0.5`: trigger approximately every second eligible event
  - `0.0`: no trigger
- Reset trigger accumulators in engine reset path.
- Updated README animation-table docs to clarify fractional trigger-rate behavior.

## Evidence
- Host tests:
  - `no_beat_policy_test: OK`
  - `beat_event_flow_test: OK`
  - `beat_scheduler_integration_test: OK`
- PlatformIO firmware build:
  - `pio run -e esp32-s3-super-mini-idf`: SUCCESS

## Commands
- `./scripts/run_host_tests.sh` -> success
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` -> success

## Tests/Checks
- Run: host tests + esp32-s3 firmware build.

## Risks/Rollback
- Risk: fractional trigger scaling intentionally drops some eligible trigger events when scale < 1.
- Rollback: revert changes in `main/animation_engine.cpp` trigger gating section and `main/animation_config_table.*` trigger outputs.

## Open Questions
- None.

## Next steps
- If desired, tune default table values in `main/animation_config_table.cpp` for each phase.

## Diff tour
- `main/animation_config_table.h`: added resolved trigger scale outputs.
- `main/animation_config_table.cpp`: changed trigger resolution from threshold boolean behavior to scale + enable.
- `main/animation_engine.cpp`: added deterministic fractional event gating for beat/fallback spawns.
- `README.md`: documented fractional trigger semantics.
