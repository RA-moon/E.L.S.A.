# Architecture Overview

## Modules
- `main/main.cpp`: Setup + task wiring.
- `main/animation_engine.cpp`: Beat-to-wave rendering, brightness pulse, wave scheduling.
- `main/animation_manager.cpp`: Animation frame sets and auto switching logic.
- `main/audio_processor.cpp`: I2S capture, FFT, beat detection, tempo estimate.
- `main/audio_scheduler.cpp`: Audio task or timed polling.
- `main/wave_position.cpp`: Wave motion, spacing, and lifetime.
- `main/waveform.cpp`, `main/frame_interpolation.cpp`: Wave envelope + frame rendering.
- `main/hair_strip.cpp`: Independent hair strip animation.
- `main/led_strip_driver.c`: RMT-based WS2812 output.

## Data Flow
1. Audio samples are captured over I2S and transformed by FFT in `audio_processor`.
2. Beat detection produces `lastBeat` + `strength` and a smoothed tempo estimate.
3. `animation_engine` schedules and advances waves based on tempo/strength.
4. The current animation frames are blended into LED output.
5. A global brightness pulse is applied after rendering.

## Loop Responsibilities
- Audio: `audioSchedulerRun()` (task or timed polling).
- Render: `runLedAnimation()` and optional hair strip update.
- Output: `led_strip_device_show()`.
