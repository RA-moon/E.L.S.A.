#pragma once

// === Hardware config ===
// ESP32-S3 Super Mini pinout (GPIO1-13 + TX/RX).
#define DATA_PIN1         1
#define NUM_LEDS1         120

// Optional 2nd strip ("hair").
// You mentioned the hair data line is on GPIO2; change as needed.
// Default is off (override with -DENABLE_HAIR_STRIP=1).
#ifndef ENABLE_HAIR_STRIP
#define ENABLE_HAIR_STRIP  0
#endif
#define DATA_PIN2          2
#define NUM_LEDS2          44

// Hair strip animation (independent of beat-driven brain strip).
#define HAIR_BRIGHTNESS            255   // 0..255
#define HAIR_SPEED_RAINBOW         10   // hue delta per update
#define HAIR_SPEED_FADE             5   // brightness delta per update
#define HAIR_UPDATE_MS             30
#define HAIR_COLOR_CYCLE_DURATION_MS 1800000UL  // 30 minutes
#define HAIR_RAINBOW_END1           32
#define HAIR_FADE_START             33
#define HAIR_FADE_END               39
#define HAIR_RAINBOW_START2          40
#define HAIR_RAINBOW_END2            43

#define BRIGHTNESS1       80   // baseline (0..255)
#define DELAY_MS          10
#define NO_BEAT_FALLBACK_MS 800
#define AUDIO_INTERVAL    15
#define MAX_ACTIVE_WAVES  20
#define WAVE_SPACING_MIX  0.35f
#define WAVE_SPACING_INTERVAL_MS 60
#define WAVE_NOSE_MIN 0.2f
#define WAVE_NOSE_MAX 3.0f
#define WAVE_WIDTH_SCALE 1.5f

// Run audio processing in a dedicated FreeRTOS task.
#ifndef AUDIO_TASK_ENABLE
#define AUDIO_TASK_ENABLE 1
#endif

// === Test mode ===
// Set to 1 to blink white on the first TEST_LED_COUNT LEDs (matching your Arduino IDE test).
#ifndef TEST_SOLID_COLOR
#define TEST_SOLID_COLOR  0
#endif
#define TEST_LED_COUNT    30

// === Web telemetry (beat/pattern output) ===
#ifndef ENABLE_WEB_TELEMETRY
#define ENABLE_WEB_TELEMETRY  0
#endif
#ifndef ENABLE_CONFIG_ENDPOINT
#define ENABLE_CONFIG_ENDPOINT 0
#endif
#define WEB_SERVER_PORT       80
#define WIFI_CONNECT_TIMEOUT_MS 12000
// Optional Wi-Fi keepalive/reconnect (disabled by default).
// Note: reconnect attempts can block, depending on the Wi-Fi stack.
#ifndef ENABLE_WIFI_KEEPALIVE
#define ENABLE_WIFI_KEEPALIVE  0
#endif
#define WIFI_KEEPALIVE_INTERVAL_MS 10000
// Minimum gap between /frame responses (ms). Increase if animation stutters.
#define FRAME_MIN_INTERVAL_MS 12
// OTA updates over Wi-Fi (ArduinoOTA)
#ifndef ENABLE_OTA
#define ENABLE_OTA            1
#endif

// Beat-synced pulse envelope (applied after rendering):
// - On beat: ratio = 1.0
// - Then decays to BRIGHTNESS_MIN_RATIO over the *average* beat time
#define BEAT_DECAY_MIN_MS   160
#define BEAT_DECAY_MAX_MS  1500
#define BEAT_DECAY_EASE_OUT   1   // 1 = quadratic ease-out, 0 = linear
#define BEAT_PERIOD_EMA_ALPHA 0.05f

// Fade beat-driven state back toward startup defaults after inactivity.
#ifndef ENABLE_FADE_TO_STARTUP
#define ENABLE_FADE_TO_STARTUP 1
#endif
#define FADE_TO_STARTUP_IDLE_MS 10000
#define FADE_TO_STARTUP_DURATION_MS 10000

// Waves are triggered on detected beats. If no beats are detected for a while,
// the fallback timer will still inject occasional waves so the strip doesn't go idle.
#ifndef ENABLE_BEAT_WAVES
#define ENABLE_BEAT_WAVES     1
#endif
#ifndef ENABLE_FALLBACK_WAVES
#define ENABLE_FALLBACK_WAVES 1
#endif

// Serial debug print on every beat
#ifndef DEBUG_BEAT_TIMING
#define DEBUG_BEAT_TIMING     0
#endif
// Serial debug print on every wave
#ifndef DEBUG_WAVE_TIMING
#define DEBUG_WAVE_TIMING     0
#endif

// Physical button (active-low to GND)
#define BUTTON_PIN            4
#define BUTTON_ACTIVE_LOW     1
#define BUTTON_DEBOUNCE_MS    30
#define BUTTON_DOUBLE_TAP_MS  350

// Performance profiling (averages printed to Serial).
#ifndef PROFILE_PERF
#define PROFILE_PERF          0
#endif

// Wave envelope (relative units in animation frames).
// Min values define the baseline width (sum = 1.0).
// Max values define the peak width (sum = 4.0).
#define WAVE_ATTACK_MIN   0.2f
#define WAVE_SUSTAIN_MIN  0.3f
#define WAVE_RELEASE_MIN  0.3f
#define WAVE_DECAY_MIN    0.2f

#define WAVE_ATTACK_MAX   0.8f
#define WAVE_SUSTAIN_MAX  1.2f
#define WAVE_RELEASE_MAX  1.2f
#define WAVE_DECAY_MAX    0.8f
#define PROFILE_INTERVAL_MS   2000
