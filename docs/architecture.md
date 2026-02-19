# Architecture Overview

## Modules
- `main/main.cpp`: Setup + task wiring.
- `main/animation_engine.cpp`: Beat-to-wave rendering, pulse/width envelopes, and scheduler orchestration.
- `main/animation_manager.cpp`: Animation frame sets and auto switching logic.
- `main/audio_processor.cpp`: I2S capture, FFT beat detection, beat-event publish, tempo estimate.
- `main/audio_scheduler.cpp`: Audio task or timed polling.
- `main/wave_position.cpp`: Wave motion, spacing, and lifetime.
- `main/waveform.cpp`, `main/frame_interpolation.cpp`: Wave envelope + frame rendering.
- `main/hair_strip.cpp`: Independent hair strip animation.
- `main/led_strip_driver.c`: RMT-based WS2812 output.
- `main/beat_scheduler_policy.cpp`: Shared policy helpers for beat-flow, relax ticks, and fallback decisions.

## Data Flow
1. Audio samples are captured over I2S and transformed by FFT in `audio_processor`.
2. Beat detection publishes atomic beat events as `{strength, timestampMs, isReal}` plus a smoothed tempo estimate.
3. `animation_engine` consumes beat events and resolves spawn reason precedence:
   real beat/fake beat -> relax tick -> fallback overdue.
4. The current animation frames are blended into LED output.
5. A global brightness pulse and beat-width pulse are applied after per-wave rendering.

## Concurrency Model
- Audio and render run on separate tasks when `AUDIO_TASK_ENABLE=1`.
- Shared audio state (detector config, average beat interval, telemetry snapshots) is synchronized in `audio_processor`.
- Beat event handoff uses a dedicated critical section so each event is consumed once.

## Loop Responsibilities
- Audio: `audioSchedulerRun()` (task or timed polling).
- Render: `runLedAnimation()` and optional hair strip update.
- Output: `led_strip_device_show()`.
