# Architecture Overview

## Modules
- `src/main.cpp`: Setup + main loop wiring only.
- `src/animation_engine.cpp`: Beat-to-wave rendering, brightness pulse, wave scheduling.
- `src/animation_manager.cpp`: Animation frame sets and auto switching logic.
- `src/audio_processor.cpp`: I2S capture, FFT, beat detection, tempo estimate.
- `src/audio_scheduler.cpp`: Audio task or timed polling.
- `src/wave_position.cpp`: Wave motion, spacing, and lifetime.
- `src/waveform.cpp`, `src/frame_interpolation.cpp`: Wave envelope + frame rendering.
- `src/hair_strip.cpp`: Independent hair strip animation.
- `src/controls.cpp`: Button handling.
- `src/networking.cpp`: Wi-Fi + OTA setup.
- `src/web_telemetry.cpp`: Web UI endpoints.

## Data Flow
1. Audio samples are captured over I2S and transformed by FFT in `audio_processor`.
2. Beat detection produces `lastBeat` + `strength` and a smoothed tempo estimate.
3. `animation_engine` schedules and advances waves based on tempo/strength.
4. The current animation frames are blended into LED output.
5. A global brightness pulse is applied after rendering.
6. Telemetry endpoints expose status and a live `/frame` feed when enabled.

## Loop Responsibilities
- Audio: `audioSchedulerRun()` (task or timed polling).
- Input: `handleButton()` for single/double-tap control.
- Render: `runLedAnimation()` and optional hair strip update.
- Output: `FastLED.show()`.
- Networking: `webTelemetryPoll()`, `pollWiFi()`, `handleOta()` (as enabled).
