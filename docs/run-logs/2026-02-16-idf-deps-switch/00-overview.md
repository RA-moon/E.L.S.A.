# Run Log: 2026-02-16-idf-deps-switch

## Context
- Step 1: Restore 4MB flash configuration (hardware confirmed 4MB).
- Step 2: Switch esp-dsp to full source component.
- Step 3: Document esp-dsp dependency in README.

## Changes
- Switched PlatformIO envs back to the 4MB board and restored `sdkconfig.defaults` to 4MB.
- Kept `idf_component.yml`, but vendored esp-dsp from source under `components/esp-dsp`.
- Updated `main/CMakeLists.txt` to require `esp-dsp`.
- Updated README ESP-IDF notes to reflect the vendored esp-dsp source component.

## Evidence
- Step 1 build failed due to missing esp-dsp component (before vendoring).
- Step 2 build succeeded after cloning esp-dsp into `components/esp-dsp` (warning persists: expected 4MB, found 2MB).
- Step 3 build succeeded after README update (warning persists).

## Commands
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` (step 1, failed: esp-dsp missing)
- `git clone https://github.com/espressif/esp-dsp.git components/esp-dsp`
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` (step 2, success; warning persists)
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` (step 3, success; warning persists)

## Tests/Checks
- `pio run -e esp32-s3-super-mini-idf` (step 1 failed: esp-dsp missing)
- `pio run -e esp32-s3-super-mini-idf` (step 2 pass; flash size warning persists)
- `pio run -e esp32-s3-super-mini-idf` (step 3 pass; flash size warning persists)

## Risks/Rollback
- Risk: Flash size warning persists even with 4MB configuration (esptool reports 2MB).
- Rollback: remove `components/esp-dsp` and revert `main/CMakeLists.txt` to the previous dependency if needed.

## Open Questions
- Do you want me to chase down the flash size mismatch warning (hardware is 4MB, esptool reports 2MB)?

## Next Steps
- If desired, resolve the flash size mismatch warning for the ESP-IDF build.

## Diff Tour
- `platformio.ini`: switched envs back to the 4MB board; kept component manager flag.
- `sdkconfig.defaults`: flash size set back to 4MB.
- `components/esp-dsp`: vendored esp-dsp source component.
- `main/CMakeLists.txt`: `REQUIRES esp-dsp`.
- `idf_component.yml`: esp-dsp dependency.
- `README.md`: ESP-IDF section updated for the vendored esp-dsp source.
