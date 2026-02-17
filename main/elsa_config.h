#pragma once

// Defaults here can be overridden via -D... build flags in platformio.ini.
// Some values are used as startup defaults for runtime_config (see runtime_config.cpp)
// and are clamped there; those clamps are noted below.

// === Hardware / Pins ===
// Brain strip on GPIO1, Hair strip on GPIO2.
// DATA_PIN* must be a valid ESP32-S3 GPIO for RMT.
#define DATA_PIN1                 1   // Brain strip data pin
#define NUM_LEDS1                 120 // Brain strip length (affects buffer size + render cost)

// Compile-time toggle for the hair strip.
#ifndef ENABLE_HAIR_STRIP
#define ENABLE_HAIR_STRIP         1   // 1 = init + render hair strip, 0 = disabled
#endif
#define DATA_PIN2                 2   // Hair strip data pin
#define NUM_LEDS2                 44  // Hair strip length

// === Render Loop / FPS ===
// Baseline brightness used to initialize g_config.brightness (clamped 0..255).
#define BRIGHTNESS1               80
// Task delay used when RENDER_TARGET_FPS=0 (tick-paced).
#define DELAY_MS                  10
// Optional render FPS target (0 = use DELAY_MS tick pacing).
// Timing is tick-quantized (no busy-wait).
// SSOT: set via build flags in platformio.ini (see build_flags: -DRENDER_TARGET_FPS=100).
#ifndef RENDER_TARGET_FPS
#define RENDER_TARGET_FPS         0
#endif
// 1 = skip rendering when a previous RMT transfer is still in flight.
// Effect: reduces CPU work, may reduce effective FPS under load.
#ifndef SKIP_RENDER_WHEN_BUSY
#define SKIP_RENDER_WHEN_BUSY     1
#endif
// Log measured render FPS every N ms (0 = disabled). Adds log overhead.
#ifndef RENDER_FPS_LOG_MS
#define RENDER_FPS_LOG_MS         0
#endif

// === Hair Strip Animation (independent of beat-driven brain strip) ===
// Brightness scaling for hair strip only (0..255).
#define HAIR_BRIGHTNESS           255
// Hue increment per update (0..255). Higher = faster rainbow movement.
#define HAIR_SPEED_RAINBOW        10
// Brightness increment per update (0..255). Higher = faster fade in/out.
#define HAIR_SPEED_FADE           5
// Update cadence (ms). Lower = smoother but more CPU.
#define HAIR_UPDATE_MS            30
// Duration of full color cycle (ms).
#define HAIR_COLOR_CYCLE_DURATION_MS 1800000UL  // 30 minutes
// Hair strip segment indices (0..NUM_LEDS2-1). Code clamps ranges to bounds.
// Non-rainbow segment is used for the "veins" animation.
#define HAIR_RAINBOW_END1         32
#define HAIR_FADE_START           33
#define HAIR_FADE_END             39
#define HAIR_RAINBOW_START2       40
#define HAIR_RAINBOW_END2         43

// === Audio Scheduling ===
// Audio task period (ms) when AUDIO_TASK_ENABLE=1. Lower = more responsive, more CPU.
#define AUDIO_INTERVAL            15
// Default fallback wave interval (ms) when no beats are detected.
// Used to init g_config.fallbackMs (clamped 0..10000).
#define NO_BEAT_FALLBACK_MS       800

// === Wave / Motion ===
// Max active waves (clamped 1..100). Higher = more CPU.
#define MAX_ACTIVE_WAVES          20
// Spacing recalculation throttle (ms). Lower = more accurate spacing, more CPU.
#define WAVE_SPACING_INTERVAL_MS  60
// Ratios of spacing between wave peaks (nose + gap + tail = 1.0).
// Nose/gap are clamped 0..1 and normalized if sum > 1.0.
#define WAVE_NOSE_RATIO           0.20f  // fraction of spacing used for nose
#define WAVE_GAP_RATIO            0.20f  // fraction of spacing left dark (gap)
// Tail ratio is derived as: 1 - WAVE_NOSE_RATIO - WAVE_GAP_RATIO (clamped >= 0).
// Spawn interval = beatPeriod * WAVE_SPAWN_RATIO (clamped 0.1..5.0).
#define WAVE_SPAWN_RATIO          1.0f
// 0..1 random jitter applied to spawn interval (clamped 0..1).
#define WAVE_SPAWN_JITTER         0.20f
// Base wave speed at speed control = 0 (clamped 0..5).
#define WAVE_SPEED_BASE           0.5f
// Speed range multiplier (clamped 1..5). >1 expands range around base.
#define WAVE_SPEED_RANGE          2.0f
// Global speed multiplier (clamped 0.1..5).
#define WAVE_SPEED_MULTIPLIER     1.0f
// Base FPS for wave speed scaling (used if render FPS not known).
#define WAVE_SPEED_BASE_FPS       60.0f
// Spawn a wave on every Nth beat (clamped 1..32).
#define BEAT_WAVE_EVERY_N         1

// Wave falloff curves (cubic bezier y control points, 0..1).
// 0.0/1.0 matches smoothstep; lower P1 or higher P2 changes easing.
#define WAVE_FALLOFF_NOSE_P1      0.0f
#define WAVE_FALLOFF_NOSE_P2      1.0f
#define WAVE_FALLOFF_TAIL_P1      0.0f
#define WAVE_FALLOFF_TAIL_P2      1.0f
// Optional LUT for falloff curves (precomputed at init and when updated).
// Higher size = smoother curve, more memory.
#ifndef WAVE_FALLOFF_LUT_ENABLE
#define WAVE_FALLOFF_LUT_ENABLE   0
#endif
#ifndef WAVE_FALLOFF_LUT_SIZE
#define WAVE_FALLOFF_LUT_SIZE     128
#endif

// === Beat / Pulse Response ===
// Beat decay window (ms). Min clamped 50..5000, max clamped 50..10000; swapped if min > max.
#define BEAT_DECAY_MIN_MS         160
#define BEAT_DECAY_MAX_MS         1500
// 1 = quadratic ease-out, 0 = linear.
#define BEAT_DECAY_EASE_OUT       1
// Smoothing for beat period EMA (0..1). Lower = smoother, slower response.
#define BEAT_PERIOD_EMA_ALPHA     0.05f
// Minimum brightness ratio during beat decay (0..1).
#define BEAT_PULSE_MIN_RATIO      0.10f
// Decay time multiplier (0.1..5.0). Higher = slower decay.
#define BEAT_PULSE_DECAY_RATIO    1.00f

// Fade beat-driven state back toward startup defaults after inactivity.
#ifndef ENABLE_FADE_TO_STARTUP
#define ENABLE_FADE_TO_STARTUP    1
#endif
// Idle time before fade starts (ms).
#define FADE_TO_STARTUP_IDLE_MS   10000
// Fade duration once idle (ms).
#define FADE_TO_STARTUP_DURATION_MS 20000

// Enable beat-triggered waves and fallback waves.
// Fallback waves trigger after silence even when beat waves are enabled.
#ifndef ENABLE_BEAT_WAVES
#define ENABLE_BEAT_WAVES         1
#endif
#ifndef ENABLE_FALLBACK_WAVES
#define ENABLE_FALLBACK_WAVES     1
#endif

// === Animation Switching ===
// Relative BPM change required to auto-switch animations (e.g. 0.20 = 20%).
#define BPM_SWITCH_THRESHOLD      0.20f
// Window length to measure BPM change (ms).
#define BPM_SWITCH_WINDOW_MS      5000
// Fallback auto-switch interval when BPM is stable (ms).
#define AUTO_SWITCH_INTERVAL_MS   60000

// === Tasks ===
// Audio task enabled (1) or run audio in render loop (0).
#ifndef AUDIO_TASK_ENABLE
#define AUDIO_TASK_ENABLE         1
#endif
// FreeRTOS priorities (higher = higher priority).
#ifndef AUDIO_TASK_PRIORITY
#define AUDIO_TASK_PRIORITY       5
#endif
#ifndef RENDER_TASK_PRIORITY
#define RENDER_TASK_PRIORITY      4
#endif
// Core affinity (ESP32-S3 has core 0/1).
#ifndef AUDIO_TASK_CORE
#define AUDIO_TASK_CORE           0
#endif
#ifndef RENDER_TASK_CORE
#define RENDER_TASK_CORE          1
#endif

// === Test Mode ===
// Set to 1 to blink white on the first TEST_LED_COUNT LEDs (overrides animations).
#ifndef TEST_SOLID_COLOR
#define TEST_SOLID_COLOR          0
#endif
#define TEST_LED_COUNT            30

// === Debug / Profiling ===
// Enables periodic performance logs (render + audio). Adds log overhead.
#ifndef PROFILE_PERF
#define PROFILE_PERF              0
#endif
#define PROFILE_INTERVAL_MS       2000

// === Power ===
// Dynamic frequency scaling (requires CONFIG_PM_ENABLE in sdkconfig).
#ifndef ENABLE_DYNAMIC_FREQ_SCALING
#define ENABLE_DYNAMIC_FREQ_SCALING 0
#endif
#define PM_MAX_CPU_MHZ            240
#define PM_MIN_CPU_MHZ            80

// === Unused / Reserved (no effect in current code) ===
// Button support is not wired up yet.
#define BUTTON_PIN                4
#define BUTTON_ACTIVE_LOW         1
#define BUTTON_DEBOUNCE_MS        30
#define BUTTON_DOUBLE_TAP_MS      350
// Beat/wave debug flags are not referenced.
#ifndef DEBUG_BEAT_TIMING
#define DEBUG_BEAT_TIMING         0
#endif
#ifndef DEBUG_WAVE_TIMING
#define DEBUG_WAVE_TIMING         0
#endif
// Web / OTA toggles are defined but not used in the current codebase.
#ifndef ENABLE_WEB_TELEMETRY
#define ENABLE_WEB_TELEMETRY      0
#endif
#ifndef ENABLE_CONFIG_ENDPOINT
#define ENABLE_CONFIG_ENDPOINT    0
#endif
#define WEB_SERVER_PORT           80
#define WIFI_CONNECT_TIMEOUT_MS   12000
#ifndef ENABLE_WIFI_KEEPALIVE
#define ENABLE_WIFI_KEEPALIVE     0
#endif
#define WIFI_KEEPALIVE_INTERVAL_MS 10000
#define FRAME_MIN_INTERVAL_MS     12
#ifndef ENABLE_OTA
#define ENABLE_OTA                1
#endif
