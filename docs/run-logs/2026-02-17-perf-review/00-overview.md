# Run Log: 2026-02-17-perf-review

## Context
- Implement performance improvements (render pacing, skip render when RMT busy, fallback waves on silence, and render FPS logging) with tests between changes when requested.

## Changes
- Added `RENDER_TARGET_FPS` config and microsecond pacing path in the render task; default keeps `DELAY_MS` behavior. (`main/elsa_config.h`, `main/main.cpp`)
- Set `RENDER_TARGET_FPS` default to `100` (tick-aligned). (`main/elsa_config.h`)
- Added `RENDER_TARGET_FPS=100` to `platformio.ini` build flags. (`platformio.ini`)
- Skip rendering when the brain strip RMT transfer is in-flight via `led_strip_device_is_busy()` (configurable with `SKIP_RENDER_WHEN_BUSY`). (`main/led_strip_driver.{h,c}`, `main/main.cpp`, `main/elsa_config.h`)
- Optional render FPS logging via `RENDER_FPS_LOG_MS`. (`main/elsa_config.h`, `main/main.cpp`)
- Fallback waves now trigger after `fallbackMs` of no real beats even when beat waves are enabled. (`main/animation_engine.cpp`)
- Documented `RENDER_TARGET_FPS`, `SKIP_RENDER_WHEN_BUSY`, and `RENDER_FPS_LOG_MS`. (`README.md`)

## Evidence
- Render pacing uses `RENDER_TARGET_FPS` (0 = tick-based), otherwise `esp_timer`-based pacing with microsecond waits.
- `led_strip_device_is_busy()` exposes `in_flight` for render gating.
- Render FPS log emits `render fps=...` every `RENDER_FPS_LOG_MS`.
- Fallback waves use `getLastRealBeatMs()` and `s_lastBeatMs` to decide silence.

## Commands (state-changing only) + result
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini` (failed: unknown env)
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` (ok)
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` (failed: CMake configure error; missing `component_requires.temp.cmake`, git repo detection failure)
- `rm -rf .pio/build/esp32-s3-super-mini-idf` (ok)
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` (ok)
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` (ok)
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` (ok)
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` (ok)
- `/Users/ramunriklin/.platformio/penv/bin/pio run -e esp32-s3-super-mini-idf` (ok)

## Tests/Checks
- `pio run -e esp32-s3-super-mini-idf` x6 (passed); x1 (failed: CMake configure error)

## Config Notes
- `RENDER_TARGET_FPS`: target FPS (0 uses `DELAY_MS`); default `100`; read in `main/main.cpp`.
- `SKIP_RENDER_WHEN_BUSY`: skip render if RMT in-flight; default `1`; read in `main/main.cpp`.
- `RENDER_FPS_LOG_MS`: log render FPS every N ms (0 disables); default `0`; read in `main/main.cpp`.
- `RENDER_TARGET_FPS` is also set in `platformio.ini` build flags to `100`.

## Risks/Rollback
- Risk: non-tick-aligned `RENDER_TARGET_FPS` may busy-wait for up to one RTOS tick.
- Rollback: revert changes in `main/main.cpp`, `main/elsa_config.h`, `main/led_strip_driver.{h,c}`, `main/animation_engine.cpp`, `README.md`.

## Open questions + Next steps
- None.
