#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <WiFiMulti.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "animtated_circles.h"
#include "animtated_lines.h"
#include "animtated_circles_reversed.h"
#include "animtated_lines_reversed.h"
#include "frame_interpolation.h"
#include "waveform.h"
#include "wave_position.h"
#include "audio_processor.h"
#include "animation_manager.h"

// Optional WiFi secrets (kept out of git).
#if defined(__has_include)
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif
#endif

// === Hardware config ===
// ESP32-S3 Super Mini pinout (GPIO1-13 + TX/RX).
#define DATA_PIN1         1
#define NUM_LEDS1         120

// Optional 2nd strip ("hair").
// You mentioned the hair data line is on GPIO2; change as needed.
// Keep it disabled for now to free CPU time for audio/FFT.
#define ENABLE_HAIR_STRIP  1
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
#define AUDIO_TASK_ENABLE 1

// === Test mode ===
// Set to 1 to blink white on the first TEST_LED_COUNT LEDs (matching your Arduino IDE test).
#define TEST_SOLID_COLOR  0
#define TEST_LED_COUNT    30

// === Web telemetry (beat/pattern output) ===
#define ENABLE_WEB_TELEMETRY  0
#define ENABLE_CONFIG_ENDPOINT 0
#define WEB_SERVER_PORT       80
#define WIFI_CONNECT_TIMEOUT_MS 12000
// Optional Wi-Fi keepalive/reconnect (disabled by default).
// Note: reconnect attempts can block, depending on the Wi-Fi stack.
#define ENABLE_WIFI_KEEPALIVE  0
#define WIFI_KEEPALIVE_INTERVAL_MS 10000
// Minimum gap between /frame responses (ms). Increase if animation stutters.
#define FRAME_MIN_INTERVAL_MS 12
// OTA updates over Wi-Fi (ArduinoOTA)
#define ENABLE_OTA            1

#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME          "E.L.S.A."
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD          ""
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Beat-synced pulse envelope (applied after rendering):
// - On beat: ratio = 1.0
// - Then decays to BRIGHTNESS_MIN_RATIO over the *average* beat time
#define BEAT_DECAY_MIN_MS   160
#define BEAT_DECAY_MAX_MS  1500
#define BEAT_DECAY_EASE_OUT   1   // 1 = quadratic ease-out, 0 = linear
#define BEAT_PERIOD_EMA_ALPHA 0.05f

// Waves are triggered on detected beats. If no beats are detected for a while,
// the fallback timer will still inject occasional waves so the strip doesn't go idle.
#define ENABLE_BEAT_WAVES     1
#define ENABLE_FALLBACK_WAVES 1

// Serial debug print on every beat
#define DEBUG_BEAT_TIMING     0
// Serial debug print on every wave
#define DEBUG_WAVE_TIMING     0

// Physical button (active-low to GND)
#define BUTTON_PIN            4
#define BUTTON_ACTIVE_LOW     1
#define BUTTON_DEBOUNCE_MS    30
#define BUTTON_DOUBLE_TAP_MS  350

// Performance profiling (averages printed to Serial).
#define PROFILE_PERF          0

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

#if PROFILE_PERF
static uint64_t s_audioAccumUs = 0;
static uint32_t s_audioCount = 0;
static uint64_t s_animAccumUs = 0;
static uint32_t s_animCount = 0;
static uint64_t s_showAccumUs = 0;
static uint32_t s_showCount = 0;
static uint32_t s_lastProfileMs = 0;
#endif

CRGB leds1[NUM_LEDS1];

#if ENABLE_HAIR_STRIP
CRGB leds2[NUM_LEDS2];

static uint8_t s_hairHueOffset = 0;
static int s_hairFadeBrightness = 0;
static int s_hairFadeDirection = 1;
static uint32_t s_hairColorCycleStartMs = 0;
static uint32_t s_lastHairUpdateMs = 0;

static inline void updateHairStrip(uint32_t nowMs) {
  if ((int32_t)(nowMs - s_lastHairUpdateMs) < (int32_t)HAIR_UPDATE_MS) return;
  s_lastHairUpdateMs = nowMs;

  fill_solid(leds2, NUM_LEDS2, CRGB::Black);

  const int lastIndex = (NUM_LEDS2 > 0) ? (NUM_LEDS2 - 1) : -1;
  const int r1Start = 0;
  const int r1End = min((int)HAIR_RAINBOW_END1, lastIndex);
  const int r2Start = max((int)HAIR_RAINBOW_START2, 0);
  const int r2End = min((int)HAIR_RAINBOW_END2, lastIndex);

  int activeRainbowLeds = 0;
  if (r1End >= r1Start) activeRainbowLeds += (r1End - r1Start + 1);
  if (r2End >= r2Start) activeRainbowLeds += (r2End - r2Start + 1);

  int rainbowIndex = 0;
  if (activeRainbowLeds > 0) {
    for (int i = r1Start; i <= r1End; i++) {
      const uint8_t hue = s_hairHueOffset + (uint8_t)(rainbowIndex * 255 / activeRainbowLeds);
      leds2[i] = CHSV(hue, 255, 255);
      rainbowIndex++;
    }
    for (int i = r2Start; i <= r2End; i++) {
      const uint8_t hue = s_hairHueOffset + (uint8_t)(rainbowIndex * 255 / activeRainbowLeds);
      leds2[i] = CHSV(hue, 255, 255);
      rainbowIndex++;
    }
  }

  if (s_hairColorCycleStartMs == 0) {
    s_hairColorCycleStartMs = nowMs;
  }
  const uint32_t elapsed = (nowMs - s_hairColorCycleStartMs) % HAIR_COLOR_CYCLE_DURATION_MS;
  const float phase = (float)elapsed / (HAIR_COLOR_CYCLE_DURATION_MS / 2.0f);
  const float interpFactor = (phase <= 1.0f) ? phase : (2.0f - phase);

  const int startHue = 96;
  const int endHue = 160;
  const uint8_t interpHue = (uint8_t)(startHue + (int)((endHue - startHue) * interpFactor));

  int effectiveBrightness = s_hairFadeBrightness;
  if (s_hairFadeBrightness > 80 && s_hairFadeBrightness < 180) {
    effectiveBrightness += (int)random(-30, 30);
    effectiveBrightness = constrain(effectiveBrightness, 0, 255);
  }
  const CRGB fadeColor = CHSV(interpHue, 255, (uint8_t)effectiveBrightness);

  const int fStart = max((int)HAIR_FADE_START, 0);
  const int fEnd = min((int)HAIR_FADE_END, lastIndex);
  if (fEnd >= fStart) {
    for (int i = fStart; i <= fEnd; i++) {
      leds2[i] = fadeColor;
    }
  }

  if (HAIR_BRIGHTNESS < 255) {
    nscale8_video(leds2, NUM_LEDS2, (uint8_t)HAIR_BRIGHTNESS);
  }

  s_hairHueOffset = (uint8_t)(s_hairHueOffset + HAIR_SPEED_RAINBOW);
  s_hairFadeBrightness += s_hairFadeDirection * HAIR_SPEED_FADE;
  if (s_hairFadeBrightness >= 255) {
    s_hairFadeBrightness = 255;
    s_hairFadeDirection = -1;
  } else if (s_hairFadeBrightness <= 0) {
    s_hairFadeBrightness = 0;
    s_hairFadeDirection = 1;
  }
}
#endif

static uint32_t lastWaveTime = 0;
static uint32_t lastWaveIntervalMs = 0;
static uint32_t lastWavePeriodMs = 0;
static uint32_t nextWaveDueMs = 0;
static uint32_t lastAudioTime = 0;
static float smoothedBeatPeriodMs = 0.0f;

// Beat envelope state
static uint32_t lastBeatMs = 0;
static float lastBeatStrength = 0.7f;
static uint32_t lastBeatIntervalMs = 0;

// Global brightness pulse envelope (applied after rendering).
#define BRIGHTNESS_MIN_RATIO 0.30f
#define BRIGHTNESS_MAX_RATIO 1.00f

#if AUDIO_TASK_ENABLE
static TaskHandle_t s_audioTaskHandle = nullptr;
static void audioTask(void* param) {
  (void)param;
  const TickType_t delayTicks = pdMS_TO_TICKS(AUDIO_INTERVAL);
  for (;;) {
    processAudio();
    vTaskDelay(delayTicks);
  }
}
#endif

// Button state
static bool s_buttonStable = false;
static bool s_buttonLastRead = false;
static uint32_t s_buttonLastChangeMs = 0;
static uint32_t s_buttonLastTapMs = 0;
static bool s_buttonWaitingSecondTap = false;

struct RuntimeConfig {
  uint8_t brightness;
  uint16_t beatDecayMinMs;
  uint16_t beatDecayMaxMs;
  int16_t pulseLeadMs;
  uint16_t fallbackMs;
  uint8_t maxActiveWaves;
  bool enableBeatWaves;
  bool enableFallbackWaves;
  bool animationAuto;
  int animationIndex;
  float energyEmaAlpha;
  float fluxEmaAlpha;
  float fluxThreshold;
  float fluxRiseFactor;
  uint16_t minBeatIntervalMs;
  uint16_t avgBeatMinMs;
  uint16_t avgBeatMaxMs;
};

static RuntimeConfig g_config = {
  BRIGHTNESS1,
  BEAT_DECAY_MIN_MS,
  BEAT_DECAY_MAX_MS,
  0,
  NO_BEAT_FALLBACK_MS,
  MAX_ACTIVE_WAVES,
  (ENABLE_BEAT_WAVES != 0),
  (ENABLE_FALLBACK_WAVES != 0),
  true,
  0,
  0.10f,
  0.20f,
  1.7f,
  0.12f,
  430,
  430,
  800
};

#if (ENABLE_WEB_TELEMETRY || ENABLE_OTA)
static bool setupWiFi();
#endif

#if ENABLE_WEB_TELEMETRY
struct BeatTelemetry {
  uint32_t beatCount;
  uint32_t lastBeatMs;
  float lastBeatStrength;
  float avgBeatIntervalMs;
  float bpm;
  uint32_t lastWaveMs;
  uint32_t lastWaveIntervalMs;
  uint32_t wavePeriodMs;
  uint32_t nextWaveInMs;
  uint32_t activeWaves;
  int animationIndex;
  const char* animationName;
  float baseBrightnessRatio;
  float pulseRatio;
};

static BeatTelemetry telemetry = {
  0,
  0,
  0.0f,
  0.0f,
  0.0f,
  0,
  0,
  0,
  0,
  0,
  0,
  "unknown",
  0.0f,
  1.0f
};

static WebServer server(WEB_SERVER_PORT);
static uint32_t lastFrameSendMs = 0;

static void handleRoot();
static void handleStatus();
static void handleFrame();
static void handleConfig();
static void setupWebServer();
static void pollWebServer();
#endif
static void handleButton();

static bool wifiConnected = false;
#if (ENABLE_WEB_TELEMETRY || ENABLE_OTA)
#if defined(WIFI_MULTI_ENABLED) && WIFI_MULTI_ENABLED
static WiFiMulti s_wifiMulti;
static bool s_wifiMultiConfigured = false;
#endif
#if ENABLE_WIFI_KEEPALIVE
static uint32_t s_lastWifiCheckMs = 0;
#endif
#endif

static inline void showStrips() {
  FastLED.show();
}

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static inline uint8_t clampU8(int value, int lo, int hi) {
  if (value < lo) return (uint8_t)lo;
  if (value > hi) return (uint8_t)hi;
  return (uint8_t)value;
}

static inline float clampf(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

static inline float lerpf(float a, float b, float t) {
  return a + (b - a) * t;
}

static inline float beatPulseRatio(float beatPeriodMs, uint32_t nowMs) {
  if (lastBeatMs == 0 || beatPeriodMs <= 1.0f) return BRIGHTNESS_MAX_RATIO;

  const uint32_t dt = nowMs - lastBeatMs;
  if (dt >= (uint32_t)beatPeriodMs) return BRIGHTNESS_MIN_RATIO;

  float e = 1.0f - ((float)dt / beatPeriodMs); // 1..0
#if BEAT_DECAY_EASE_OUT
  e *= e;
#endif
  const float ratio = BRIGHTNESS_MIN_RATIO + ((BRIGHTNESS_MAX_RATIO - BRIGHTNESS_MIN_RATIO) * e);
  return clampf(ratio, BRIGHTNESS_MIN_RATIO, BRIGHTNESS_MAX_RATIO);
}

static inline void applyPulseToStrip(CRGB* leds, uint16_t count, float ratio) {
  if (ratio >= 0.999f) return;
  if (ratio <= 0.0f) {
    fill_solid(leds, count, CRGB::Black);
    return;
  }
  uint16_t scale = (uint16_t)lroundf(ratio * 255.0f);
  if (scale > 255) scale = 255;
  for (uint16_t i = 0; i < count; i++) {
    const uint16_t r = (uint16_t)leds[i].r * scale;
    const uint16_t g = (uint16_t)leds[i].g * scale;
    const uint16_t b = (uint16_t)leds[i].b * scale;
    leds[i].r = (uint8_t)((r + 127) / 255);
    leds[i].g = (uint8_t)((g + 127) / 255);
    leds[i].b = (uint8_t)((b + 127) / 255);
  }
}

static inline void computeWaveWidths(float strength, float* outNose, float* outTail) {
  const float t = clampf(strength, 0.0f, 1.0f);
  const float attack = lerpf(WAVE_ATTACK_MIN, WAVE_ATTACK_MAX, t);
  const float sustain = lerpf(WAVE_SUSTAIN_MIN, WAVE_SUSTAIN_MAX, t);
  const float release = lerpf(WAVE_RELEASE_MIN, WAVE_RELEASE_MAX, t);
  const float decay = lerpf(WAVE_DECAY_MIN, WAVE_DECAY_MAX, t);

  // Map A/D to the leading edge and S/R to the trailing edge.
  const float nose = attack + decay;
  const float tail = sustain + release;

  if (outNose) *outNose = nose * WAVE_WIDTH_SCALE;
  if (outTail) *outTail = tail * WAVE_WIDTH_SCALE;
}

static inline int8_t speedControlFromPeriod(uint32_t periodMs) {
  const float bpm = (periodMs > 1.0f) ? (60000.0f / (float)periodMs) : 0.0f;
  const float bpmMin = 74.0f;
  const float bpmMax = 130.0f;
  if (bpmMax <= bpmMin) return 0;

  float t = (bpm - bpmMin) / (bpmMax - bpmMin);
  t = clampf(t, 0.0f, 1.0f);

  const float speedMin = 0.05f;
  const float speedMax = 0.15f;
  const float speed = speedMin + (t * (speedMax - speedMin));
  const int sc = (int)lroundf((speed - 0.2f) * 25.0f);
  return (int8_t)constrain(sc, -10, 10);
}

static inline uint16_t clampU16(long value, long lo, long hi) {
  if (value < lo) return (uint16_t)lo;
  if (value > hi) return (uint16_t)hi;
  return (uint16_t)value;
}

static bool parseLongArg(const String& value, long* out) {
  if (value.length() == 0) return false;
  char* endPtr = nullptr;
  const long parsed = strtol(value.c_str(), &endPtr, 10);
  if (endPtr == value.c_str()) return false;
  *out = parsed;
  return true;
}

static bool parseFloatArg(const String& value, float* out) {
  if (value.length() == 0) return false;
  char* endPtr = nullptr;
  const float parsed = strtof(value.c_str(), &endPtr);
  if (endPtr == value.c_str()) return false;
  *out = parsed;
  return true;
}

static bool parseBoolArg(const String& value, bool* out) {
  if (value.length() == 0) return false;
  if (value == "1" || value == "true" || value == "on" || value == "yes") {
    *out = true;
    return true;
  }
  if (value == "0" || value == "false" || value == "off" || value == "no") {
    *out = false;
    return true;
  }
  return false;
}

static void normalizeConfig() {
  g_config.brightness = clampU8((int)g_config.brightness, 0, 255);
  g_config.beatDecayMinMs = clampU16((long)g_config.beatDecayMinMs, 50, 5000);
  g_config.beatDecayMaxMs = clampU16((long)g_config.beatDecayMaxMs, 50, 10000);
  if (g_config.beatDecayMinMs > g_config.beatDecayMaxMs) {
    const uint16_t tmp = g_config.beatDecayMinMs;
    g_config.beatDecayMinMs = g_config.beatDecayMaxMs;
    g_config.beatDecayMaxMs = tmp;
  }
  if (g_config.pulseLeadMs < -250) g_config.pulseLeadMs = -250;
  if (g_config.pulseLeadMs > 250) g_config.pulseLeadMs = 250;
  g_config.fallbackMs = clampU16((long)g_config.fallbackMs, 0, 10000);
  g_config.maxActiveWaves = clampU8((int)g_config.maxActiveWaves, 1, 100);
  if (g_config.energyEmaAlpha < 0.01f) g_config.energyEmaAlpha = 0.01f;
  if (g_config.energyEmaAlpha > 0.5f) g_config.energyEmaAlpha = 0.5f;
  if (g_config.fluxEmaAlpha < 0.01f) g_config.fluxEmaAlpha = 0.01f;
  if (g_config.fluxEmaAlpha > 0.6f) g_config.fluxEmaAlpha = 0.6f;
  if (g_config.fluxThreshold < 1.1f) g_config.fluxThreshold = 1.1f;
  if (g_config.fluxThreshold > 4.0f) g_config.fluxThreshold = 4.0f;
  if (g_config.fluxRiseFactor < 0.02f) g_config.fluxRiseFactor = 0.02f;
  if (g_config.fluxRiseFactor > 0.6f) g_config.fluxRiseFactor = 0.6f;
  g_config.minBeatIntervalMs = clampU16((long)g_config.minBeatIntervalMs, 80, 1000);
  // Hard clamp to 75-140 BPM range (800..430 ms) regardless of config updates.
  g_config.avgBeatMinMs = clampU16((long)g_config.avgBeatMinMs, 430, 800);
  g_config.avgBeatMaxMs = clampU16((long)g_config.avgBeatMaxMs, 430, 800);
  if (g_config.avgBeatMinMs > g_config.avgBeatMaxMs) {
    const uint16_t tmp = g_config.avgBeatMinMs;
    g_config.avgBeatMinMs = g_config.avgBeatMaxMs;
    g_config.avgBeatMaxMs = tmp;
  }

  const int animCount = getAnimationCount();
  if (animCount > 0) {
    if (g_config.animationIndex < 0) g_config.animationIndex = 0;
    if (g_config.animationIndex >= animCount) g_config.animationIndex = animCount - 1;
  } else {
    g_config.animationIndex = 0;
  }
}

static void applyAnimationConfig() {
  setAnimationAutoMode(g_config.animationAuto);
  setAnimationIndex(g_config.animationIndex);
}

static void applyBeatConfig() {
  BeatDetectorConfig cfg = {};
  cfg.energyEmaAlpha = g_config.energyEmaAlpha;
  cfg.fluxEmaAlpha = g_config.fluxEmaAlpha;
  cfg.fluxThreshold = g_config.fluxThreshold;
  cfg.fluxRiseFactor = g_config.fluxRiseFactor;
  cfg.minBeatIntervalMs = g_config.minBeatIntervalMs;
  cfg.avgBeatMinMs = g_config.avgBeatMinMs;
  cfg.avgBeatMaxMs = g_config.avgBeatMaxMs;
  setBeatDetectorConfig(&cfg);
}

static void handleButton() {
  const uint32_t now = millis();
  const bool raw = (digitalRead(BUTTON_PIN) == (BUTTON_ACTIVE_LOW ? LOW : HIGH));

  if (raw != s_buttonLastRead) {
    s_buttonLastRead = raw;
    s_buttonLastChangeMs = now;
  }

  if ((now - s_buttonLastChangeMs) >= BUTTON_DEBOUNCE_MS && s_buttonStable != s_buttonLastRead) {
    s_buttonStable = s_buttonLastRead;
    if (s_buttonStable) {
      if (s_buttonWaitingSecondTap && (now - s_buttonLastTapMs) <= BUTTON_DOUBLE_TAP_MS) {
        s_buttonWaitingSecondTap = false;
        g_config.animationAuto = !g_config.animationAuto;
        normalizeConfig();
        applyAnimationConfig();
      } else {
        s_buttonWaitingSecondTap = true;
        s_buttonLastTapMs = now;
      }
    }
  }

  if (s_buttonWaitingSecondTap && (now - s_buttonLastTapMs) > BUTTON_DOUBLE_TAP_MS) {
    s_buttonWaitingSecondTap = false;
    if (!g_config.animationAuto) {
      const int count = getAnimationCount();
      if (count > 0) {
        g_config.animationIndex = (g_config.animationIndex + 1) % count;
        normalizeConfig();
        applyAnimationConfig();
      }
    }
  }
}

#if ENABLE_WEB_TELEMETRY
static const char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>ESP32 Beat + Frame Telemetry</title>
    <style>
      body { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; background: #0f1217; color: #e8eef7; margin: 24px; }
      h1 { font-size: 20px; margin-bottom: 8px; }
      pre { background: #151a22; padding: 16px; border-radius: 10px; }
      .muted { color: #8b96a8; font-size: 12px; }
      #strip { width: 100%; height: 60px; image-rendering: pixelated; border-radius: 8px; background: #0b0e13; }
      .row { display: flex; gap: 12px; align-items: center; margin: 12px 0; }
      form { background: #151a22; padding: 16px; border-radius: 10px; margin-top: 16px; }
      label { display: flex; gap: 12px; align-items: center; justify-content: space-between; flex: 1; }
      input[type="number"], select { background: #0b0e13; color: #e8eef7; border: 1px solid #2a3342; border-radius: 6px; padding: 4px 8px; }
      input[type="range"] { width: 180px; }
      button { background: #2b6cb0; color: #ffffff; border: none; border-radius: 8px; padding: 8px 12px; cursor: pointer; }
      button:disabled { opacity: 0.6; cursor: default; }
      .small { color: #8b96a8; font-size: 12px; }
      .col { display: flex; flex-direction: column; gap: 4px; flex: 1; }
    </style>
  </head>
  <body>
    <h1>Beat + Frame Telemetry</h1>
    <div class="muted">Status: <code>/status</code> every 1s</div>
    <div class="muted">Frames: <code>/frame</code> (binary, 3 bytes per LED)</div>
    <div class="row">
      <div class="muted" id="fps">frame interval: -- ms</div>
      <div class="muted" id="wave-age">wave age: -- ms</div>
      <div class="muted" id="wave-interval">wave interval: -- ms</div>
      <div class="muted" id="wave-period">wave period: -- ms</div>
      <div class="muted" id="wave-next">next wave in: -- ms</div>
      <div class="muted" id="wave-count">active waves: --</div>
    </div>
    <canvas id="strip" width="120" height="1"></canvas>
    <pre id="payload">waiting...</pre>
    <script>
      const payloadEl = document.getElementById('payload');
      const fpsEl = document.getElementById('fps');
      const waveAgeEl = document.getElementById('wave-age');
      const waveIntervalEl = document.getElementById('wave-interval');
      const wavePeriodEl = document.getElementById('wave-period');
      const waveNextEl = document.getElementById('wave-next');
      const waveCountEl = document.getElementById('wave-count');
      const strip = document.getElementById('strip');
      const ctx = strip.getContext('2d');

      let ledCount = 120;
      let frameBytes = ledCount * 3;
      let imageData = ctx.createImageData(ledCount, 1);

      let intervalMs = 20;
      const minInterval = 10;
      const maxInterval = 80;

      function resizeCanvas() {
        strip.width = ledCount;
        strip.height = 1;
        imageData = ctx.createImageData(ledCount, 1);
      }


      async function fetchStatus() {
        try {
          const res = await fetch('/status', { cache: 'no-store' });
          const data = await res.json();
          payloadEl.textContent = JSON.stringify(data, null, 2);
          if (data.ledCount && data.ledCount !== ledCount) {
            ledCount = data.ledCount;
            frameBytes = data.frameBytes || (ledCount * 3);
            resizeCanvas();
          }
          if (data.lastWaveAgeMs !== undefined) {
            waveAgeEl.textContent = 'wave age: ' + data.lastWaveAgeMs + ' ms';
          }
          if (data.lastWaveIntervalMs !== undefined) {
            waveIntervalEl.textContent = 'wave interval: ' + data.lastWaveIntervalMs + ' ms';
          }
          if (data.wavePeriodMs !== undefined) {
            wavePeriodEl.textContent = 'wave period: ' + data.wavePeriodMs + ' ms';
          }
          if (data.nextWaveInMs !== undefined) {
            waveNextEl.textContent = 'next wave in: ' + data.nextWaveInMs + ' ms';
          }
          if (data.activeWaves !== undefined) {
            waveCountEl.textContent = 'active waves: ' + data.activeWaves;
          }
        } catch (err) {
          payloadEl.textContent = 'error: ' + err;
        }
      }

      async function frameTick() {
        const start = performance.now();
        try {
          const res = await fetch('/frame', { cache: 'no-store' });
          if (res.status === 200) {
            const buf = await res.arrayBuffer();
            const bytes = new Uint8Array(buf);
            if (bytes.length >= frameBytes) {
              const data = imageData.data;
              for (let i = 0; i < ledCount; i++) {
                const bi = i * 3;
                const di = i * 4;
                data[di] = bytes[bi];
                data[di + 1] = bytes[bi + 1];
                data[di + 2] = bytes[bi + 2];
                data[di + 3] = 255;
              }
              ctx.putImageData(imageData, 0, 0);
            }
            intervalMs = Math.max(minInterval, intervalMs - 1);
          } else if (res.status === 204) {
            intervalMs = Math.min(maxInterval, intervalMs + 2);
          } else {
            intervalMs = Math.min(maxInterval, intervalMs + 4);
          }
        } catch (err) {
          intervalMs = Math.min(maxInterval, intervalMs + 6);
        }

        fpsEl.textContent = 'frame interval: ' + intervalMs + ' ms';
        const elapsed = performance.now() - start;
        setTimeout(frameTick, Math.max(0, intervalMs - elapsed));
      }

      fetchStatus();
      setInterval(fetchStatus, 1000);
      frameTick();
    </script>
  </body>
</html>
)HTML";

static void handleRoot() {
  server.send_P(200, "text/html", kIndexHtml);
}

static bool updateConfigFromRequest() {
  bool changed = false;
  long value = 0;
  bool boolValue = false;

  if (server.hasArg("brightness") && parseLongArg(server.arg("brightness"), &value)) {
    g_config.brightness = clampU8((int)value, 0, 255);
    changed = true;
  }
  if (server.hasArg("beatMin") && parseLongArg(server.arg("beatMin"), &value)) {
    g_config.beatDecayMinMs = clampU16(value, 50, 5000);
    changed = true;
  }
  if (server.hasArg("beatMax") && parseLongArg(server.arg("beatMax"), &value)) {
    g_config.beatDecayMaxMs = clampU16(value, 50, 10000);
    changed = true;
  }
  if (server.hasArg("pulseLeadMs") && parseLongArg(server.arg("pulseLeadMs"), &value)) {
    if (value < -250) value = -250;
    if (value > 250) value = 250;
    g_config.pulseLeadMs = (int16_t)value;
    changed = true;
  }
  if (server.hasArg("fallbackMs") && parseLongArg(server.arg("fallbackMs"), &value)) {
    g_config.fallbackMs = clampU16(value, 0, 10000);
    changed = true;
  }
  if (server.hasArg("maxWaves") && parseLongArg(server.arg("maxWaves"), &value)) {
    g_config.maxActiveWaves = clampU8((int)value, 1, 100);
    changed = true;
  }
  if (server.hasArg("beatWaves") && parseBoolArg(server.arg("beatWaves"), &boolValue)) {
    g_config.enableBeatWaves = boolValue;
    changed = true;
  }
  if (server.hasArg("fallbackWaves") && parseBoolArg(server.arg("fallbackWaves"), &boolValue)) {
    g_config.enableFallbackWaves = boolValue;
    changed = true;
  }
  if (server.hasArg("energyEmaAlpha") && parseFloatArg(server.arg("energyEmaAlpha"), &g_config.energyEmaAlpha)) {
    changed = true;
  }
  if (server.hasArg("fluxEmaAlpha") && parseFloatArg(server.arg("fluxEmaAlpha"), &g_config.fluxEmaAlpha)) {
    changed = true;
  }
  if (server.hasArg("fluxThreshold") && parseFloatArg(server.arg("fluxThreshold"), &g_config.fluxThreshold)) {
    changed = true;
  }
  if (server.hasArg("fluxRiseFactor") && parseFloatArg(server.arg("fluxRiseFactor"), &g_config.fluxRiseFactor)) {
    changed = true;
  }
  if (server.hasArg("minBeatIntervalMs") && parseLongArg(server.arg("minBeatIntervalMs"), &value)) {
    g_config.minBeatIntervalMs = clampU16(value, 80, 1000);
    changed = true;
  }
  if (server.hasArg("avgBeatMinMs") && parseLongArg(server.arg("avgBeatMinMs"), &value)) {
    g_config.avgBeatMinMs = clampU16(value, 430, 800);
    changed = true;
  }
  if (server.hasArg("avgBeatMaxMs") && parseLongArg(server.arg("avgBeatMaxMs"), &value)) {
    g_config.avgBeatMaxMs = clampU16(value, 430, 800);
    changed = true;
  }
  if (server.hasArg("mode")) {
    const String mode = server.arg("mode");
    if (mode == "auto" || mode == "1") {
      g_config.animationAuto = true;
      changed = true;
    } else if (mode == "fixed" || mode == "manual" || mode == "0") {
      g_config.animationAuto = false;
      changed = true;
    }
  }
  if (server.hasArg("anim") && parseLongArg(server.arg("anim"), &value)) {
    g_config.animationIndex = (int)value;
    changed = true;
  }

  normalizeConfig();
  if (changed) {
    applyAnimationConfig();
    applyBeatConfig();
    updateAnimationSwitch();
  }

  return changed;
}

static String buildConfigJson() {
  const int animCount = getAnimationCount();
  String json;
  json.reserve(640);
  json += "{";
  json += "\"brightness\":" + String(g_config.brightness);
  json += ",\"beatDecayMinMs\":" + String(g_config.beatDecayMinMs);
  json += ",\"beatDecayMaxMs\":" + String(g_config.beatDecayMaxMs);
  json += ",\"pulseLeadMs\":" + String(g_config.pulseLeadMs);
  json += ",\"fallbackMs\":" + String(g_config.fallbackMs);
  json += ",\"maxActiveWaves\":" + String(g_config.maxActiveWaves);
  json += ",\"enableBeatWaves\":" + String(g_config.enableBeatWaves ? 1 : 0);
  json += ",\"enableFallbackWaves\":" + String(g_config.enableFallbackWaves ? 1 : 0);
  json += ",\"beat\":{\"energyEmaAlpha\":" + String(g_config.energyEmaAlpha, 3);
  json += ",\"fluxEmaAlpha\":" + String(g_config.fluxEmaAlpha, 3);
  json += ",\"fluxThreshold\":" + String(g_config.fluxThreshold, 3);
  json += ",\"fluxRiseFactor\":" + String(g_config.fluxRiseFactor, 3);
  json += ",\"minBeatIntervalMs\":" + String(g_config.minBeatIntervalMs);
  json += ",\"avgBeatMinMs\":" + String(g_config.avgBeatMinMs);
  json += ",\"avgBeatMaxMs\":" + String(g_config.avgBeatMaxMs);
  json += "}";
  json += ",\"animation\":{\"mode\":\"";
  json += (g_config.animationAuto ? "auto" : "fixed");
  json += "\",\"index\":" + String(getCurrentAnimationIndex());
  json += ",\"name\":\"" + String(getCurrentAnimationName()) + "\"}";
  json += ",\"animations\":[";
  for (int i = 0; i < animCount; i++) {
    if (i > 0) json += ",";
    json += "\"";
    json += getAnimationNameByIndex(i);
    json += "\"";
  }
  json += "]}";
  return json;
}

static void handleConfig() {
  updateConfigFromRequest();
  const String json = buildConfigJson();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

static void handleStatus() {
  const uint32_t now = millis();
  const uint32_t lastBeat = telemetry.lastBeatMs;
  const uint32_t age = (lastBeat > 0) ? (now - lastBeat) : 0;
  const uint32_t lastWave = telemetry.lastWaveMs;
  const uint32_t waveAge = (lastWave > 0) ? (now - lastWave) : 0;

  AudioTelemetry audio = {};
  getAudioTelemetry(&audio);

  char json[1024];
  const int n = snprintf(
    json,
    sizeof(json),
    "{\"uptimeMs\":%lu,\"beatCount\":%lu,\"lastBeatMs\":%lu,\"lastBeatAgeMs\":%lu,"
    "\"ledCount\":%u,\"frameBytes\":%u,"
    "\"lastBeatStrength\":%.3f,\"avgBeatIntervalMs\":%.1f,\"bpm\":%.1f,"
    "\"lastWaveMs\":%lu,\"lastWaveAgeMs\":%lu,\"lastWaveIntervalMs\":%lu,"
    "\"wavePeriodMs\":%lu,\"nextWaveInMs\":%lu,\"activeWaves\":%lu,"
    "\"animation\":{\"index\":%d,\"name\":\"%s\"},"
    "\"brightness\":{\"value\":%u,\"baseRatio\":%.3f,\"pulseRatio\":%.3f},"
    "\"audio\":{\"i2sOk\":%u,\"bass\":%.2f,\"bassEma\":%.2f,\"ratio\":%.2f,"
    "\"rise\":%.2f,\"threshold\":%.2f,\"riseThreshold\":%.2f,"
    "\"micRms\":%.2f,\"micPeak\":%.2f,"
    "\"intervalOk\":%u,\"above\":%u,\"rising\":%u,\"lastBeatIntervalMs\":%lu,"
    "\"fft\":{\"sampleRateHz\":%lu,\"samples\":%u,\"binWidthHz\":%.2f,"
    "\"bassMinHz\":%.1f,\"bassMaxHz\":%.1f,\"binMin\":%u,\"binMax\":%u}}}",
    (unsigned long)now,
    (unsigned long)telemetry.beatCount,
    (unsigned long)lastBeat,
    (unsigned long)age,
    (unsigned)NUM_LEDS1,
    (unsigned)(NUM_LEDS1 * 3),
    telemetry.lastBeatStrength,
    telemetry.avgBeatIntervalMs,
    telemetry.bpm,
    (unsigned long)lastWave,
    (unsigned long)waveAge,
    (unsigned long)telemetry.lastWaveIntervalMs,
    (unsigned long)telemetry.wavePeriodMs,
    (unsigned long)telemetry.nextWaveInMs,
    (unsigned long)telemetry.activeWaves,
    telemetry.animationIndex,
    telemetry.animationName ? telemetry.animationName : "unknown",
    (unsigned)g_config.brightness,
    telemetry.baseBrightnessRatio,
    telemetry.pulseRatio,
    (unsigned)(audio.i2sOk ? 1 : 0),
    audio.bass,
    audio.bassEma,
    audio.ratio,
    audio.rise,
    audio.threshold,
    audio.riseThreshold,
    audio.micRms,
    audio.micPeak,
    (unsigned)(audio.intervalOk ? 1 : 0),
    (unsigned)(audio.above ? 1 : 0),
    (unsigned)(audio.rising ? 1 : 0),
    (unsigned long)audio.lastBeatIntervalMs,
    (unsigned long)audio.sampleRateHz,
    (unsigned)audio.fftSamples,
    audio.binWidthHz,
    audio.bassMinHz,
    audio.bassMaxHz,
    (unsigned)audio.binMin,
    (unsigned)audio.binMax
  );

  if (n <= 0) {
    server.send(500, "text/plain", "format error");
    return;
  }

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

static void handleFrame() {
  const uint32_t now = millis();
  if (FRAME_MIN_INTERVAL_MS > 0 && (now - lastFrameSendMs) < FRAME_MIN_INTERVAL_MS) {
    server.send(204, "text/plain", "");
    return;
  }

  lastFrameSendMs = now;
  static uint8_t frame[NUM_LEDS1 * 3];

  for (uint16_t i = 0; i < NUM_LEDS1; i++) {
    const CRGB c = leds1[i];
    frame[(i * 3) + 0] = c.r;
    frame[(i * 3) + 1] = c.g;
    frame[(i * 3) + 2] = c.b;
  }

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(NUM_LEDS1 * 3);
  server.send(200, "application/octet-stream", "");
  server.sendContent((const char*)frame, NUM_LEDS1 * 3);
}

#endif

#if (ENABLE_WEB_TELEMETRY || ENABLE_OTA)
static bool setupWiFi() {
#if defined(WIFI_MULTI_ENABLED) && WIFI_MULTI_ENABLED
  if (WIFI_NETWORK_COUNT <= 0) {
    Serial.println("WiFi disabled (WIFI_NETWORK_COUNT is 0)");
    return false;
  }

  WiFi.mode(WIFI_STA);
  if (!s_wifiMultiConfigured) {
    for (int i = 0; i < WIFI_NETWORK_COUNT; i++) {
      if (WIFI_SSIDS[i] && strlen(WIFI_SSIDS[i]) > 0) {
        s_wifiMulti.addAP(WIFI_SSIDS[i], WIFI_PASSWORDS[i]);
      }
    }
    s_wifiMultiConfigured = true;
  }

  const uint32_t start = millis();
  while (s_wifiMulti.run() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("WiFi connected: ");
    Serial.print(WiFi.SSID());
    Serial.print(" @ ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("");
  Serial.println("WiFi connection failed");
  return false;
#else
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WiFi disabled (WIFI_SSID is empty)");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("");
  Serial.println("WiFi connection failed");
  return false;
#endif
}

#endif

#if ENABLE_WEB_TELEMETRY
static void setupWebServer() {
  if (!wifiConnected) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/frame", HTTP_GET, handleFrame);
#if ENABLE_CONFIG_ENDPOINT
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/config", HTTP_POST, handleConfig);
#endif
  server.onNotFound([]() {
    server.send(404, "text/plain", "not found");
  });
  server.begin();
  Serial.print("Web telemetry ready: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
}

static void pollWebServer() {
  if (!wifiConnected) return;
  server.handleClient();
}
#endif

#if (ENABLE_WEB_TELEMETRY || ENABLE_OTA)
#if ENABLE_WIFI_KEEPALIVE
static void pollWiFi() {
  const uint32_t now = millis();
  if ((now - s_lastWifiCheckMs) < WIFI_KEEPALIVE_INTERVAL_MS) return;
  s_lastWifiCheckMs = now;

#if defined(WIFI_MULTI_ENABLED) && WIFI_MULTI_ENABLED
  if (WIFI_NETWORK_COUNT <= 0) return;
  const wl_status_t status = s_wifiMulti.run();
  const bool connected = (status == WL_CONNECTED);
#else
  if (strlen(WIFI_SSID) == 0) return;
  WiFi.reconnect();
  const bool connected = (WiFi.status() == WL_CONNECTED);
#endif

  if (connected && !wifiConnected) {
    Serial.println("WiFi reconnected");
  }
  wifiConnected = connected;
}
#endif
#endif

void runLedAnimation() {
  const uint32_t now = millis();

#if ENABLE_BEAT_WAVES
  float beatStrength = 0.0f;
  const bool beatEvent = consumeBeat(&beatStrength);
  if (beatEvent) {
    if (lastBeatMs > 0) {
      lastBeatIntervalMs = now - lastBeatMs;
    }
    lastBeatMs = now;
    lastBeatStrength = beatStrength;

#if ENABLE_WEB_TELEMETRY
    telemetry.beatCount += 1;
    telemetry.lastBeatMs = now;
    telemetry.lastBeatStrength = beatStrength;
#endif

#if DEBUG_BEAT_TIMING
    Serial.printf("Beat: avg=%.0fms (%.1f BPM) strength=%.2f\n",
                  getAverageBeatIntervalMs(), getAverageBpm(), beatStrength);
#endif
  }
#else
  const bool beatEvent = false;
  const float beatStrength = 0.0f;
#endif

  // Use the tempo estimate from the audio module.
  float beatPeriodMs = getAverageBeatIntervalMs();
  if (beatPeriodMs < (float)g_config.beatDecayMinMs) beatPeriodMs = (float)g_config.beatDecayMinMs;
  if (beatPeriodMs > (float)g_config.beatDecayMaxMs) beatPeriodMs = (float)g_config.beatDecayMaxMs;

  // Smooth the beat period over time.
  if (smoothedBeatPeriodMs <= 0.0f) {
    smoothedBeatPeriodMs = beatPeriodMs;
  } else {
    smoothedBeatPeriodMs =
      (1.0f - BEAT_PERIOD_EMA_ALPHA) * smoothedBeatPeriodMs +
      (BEAT_PERIOD_EMA_ALPHA * beatPeriodMs);
  }

  // Base brightness envelope (relative to g_config.brightness):
  // - 100% at beat peak (when BPM is valid and recent)
  // - 70% idle if no valid BPM is detected
  float baseBrightnessRatio = 0.70f;
  float pulseRatio = 1.0f;
  const bool bpmInRange =
    (lastBeatIntervalMs >= g_config.avgBeatMinMs) &&
    (lastBeatIntervalMs <= g_config.avgBeatMaxMs);
  const bool beatRecent =
    (lastBeatMs > 0) &&
    ((now - lastBeatMs) <= (uint32_t)(g_config.avgBeatMaxMs * 2));
  if (bpmInRange && beatRecent) {
    float intervalMs = (lastBeatIntervalMs > 0) ? (float)lastBeatIntervalMs : smoothedBeatPeriodMs;
    intervalMs = clampf(intervalMs, (float)g_config.avgBeatMinMs, (float)g_config.avgBeatMaxMs);
    baseBrightnessRatio = 1.0f;
    int64_t pulseNow = (int64_t)now + (int64_t)g_config.pulseLeadMs;
    if (pulseNow < 0) pulseNow = 0;
    if (pulseNow > 0xFFFFFFFFLL) pulseNow = 0xFFFFFFFFLL;
    pulseRatio = beatPulseRatio(intervalMs, (uint32_t)pulseNow);
  }

  int frameBrightness = (int)lroundf((float)g_config.brightness * baseBrightnessRatio);
  frameBrightness = constrain(frameBrightness, 0, 255);

  const float smoothedAvgMs = clampf(smoothedBeatPeriodMs,
                                     (float)g_config.avgBeatMinMs,
                                     (float)g_config.avgBeatMaxMs);
  const float smoothedBpm = (smoothedAvgMs > 1.0f) ? (60000.0f / smoothedAvgMs) : 0.0f;
  setAutoSwitchBpm(smoothedBpm);

  updateAnimationSwitch();
  const auto& frames = getCurrentAnimationFrames();

#if ENABLE_WEB_TELEMETRY
  telemetry.avgBeatIntervalMs = smoothedAvgMs;
  telemetry.bpm = smoothedBpm;
  telemetry.animationIndex = getCurrentAnimationIndex();
  telemetry.animationName = getCurrentAnimationName();
  telemetry.baseBrightnessRatio = baseBrightnessRatio;
  telemetry.pulseRatio = pulseRatio;
#endif

  // Tell the wave engine how many frames the current animation has.
  setWaveFrameCount((int)frames.size());

  fill_solid(leds1, NUM_LEDS1, CRGB::Black);

  static uint32_t lastSpacingMs = 0;
  const size_t wavesBefore = getWaves().size();
  const bool wavesMoved = updateWaves(now);
  const size_t wavesAfterMove = getWaves().size();
  const bool wavesRemoved = wavesAfterMove < wavesBefore;
  const bool spacingDue = wavesRemoved || (wavesMoved && (now - lastSpacingMs) >= WAVE_SPACING_INTERVAL_MS);
  if (spacingDue) {
    applyWaveSpacing(WAVE_SPACING_MIX, WAVE_NOSE_MIN, WAVE_NOSE_MAX);
    lastSpacingMs = now;
  }
  const auto& waves = getWaves();

#if ENABLE_WEB_TELEMETRY
  telemetry.wavePeriodMs = 0;
  telemetry.nextWaveInMs = 0;
#endif

  for (const auto& wave : waves) {
    renderInterpolatedFrame(
      frames,
      wave.center,
      wave.hue,
      wave.tailWidth,
      wave.noseWidth,
      frameBrightness,
      wave.reverse,
      leds1,
      NUM_LEDS1
    );
  }

#if ENABLE_WEB_TELEMETRY
  telemetry.activeWaves = (uint32_t)getWaves().size();
#endif

  bool wavesAdded = false;
#if ENABLE_BEAT_WAVES
  if (g_config.enableBeatWaves) {
    const float wavePeriodMs = clampf(smoothedBeatPeriodMs,
                                      (float)g_config.avgBeatMinMs,
                                      (float)g_config.avgBeatMaxMs);
    const uint32_t periodMs = (uint32_t)lroundf(wavePeriodMs);
    if (periodMs > 0) {
      if (nextWaveDueMs == 0) {
        nextWaveDueMs = now + periodMs;
        lastWavePeriodMs = periodMs;
      } else if (periodMs != lastWavePeriodMs) {
        if (lastWaveTime > 0) {
          nextWaveDueMs = lastWaveTime + periodMs;
        } else {
          nextWaveDueMs = now + periodMs;
        }
        lastWavePeriodMs = periodMs;
      }

#if ENABLE_WEB_TELEMETRY
      telemetry.wavePeriodMs = periodMs;
      telemetry.nextWaveInMs = (now >= nextWaveDueMs) ? 0 : (nextWaveDueMs - now);
#endif

      if ((int32_t)(now - nextWaveDueMs) >= 0) {
        if (getWaves().size() >= g_config.maxActiveWaves) {
          dropOldestWave();
        }
        if (getWaves().size() < g_config.maxActiveWaves) {
          const uint32_t hue = (uint32_t)random(0, 65536);
          const int16_t hueStartDeg = (int16_t)random(-360, 361);
          const int16_t hueEndDeg = (int16_t)random(-360, 361);
          const float strength = clamp01(lastBeatStrength);
          const int8_t speedCtl = speedControlFromPeriod(periodMs);
          float nose = 1.0f;
          float tail = 1.0f;
          computeWaveWidths(strength, &nose, &tail);
          nose = clampf(nose, WAVE_NOSE_MIN, WAVE_NOSE_MAX);
          const bool reverse = (random(0, 100) < 25);
          addWave(hue, speedCtl, nose, tail, reverse, hueStartDeg, hueEndDeg);
          wavesAdded = true;
        }

        lastWaveIntervalMs = (lastWaveTime > 0) ? (now - lastWaveTime) : 0;
        lastWaveTime = now;

#if ENABLE_WEB_TELEMETRY
        telemetry.lastWaveMs = now;
        telemetry.lastWaveIntervalMs = lastWaveIntervalMs;
        telemetry.activeWaves = (uint32_t)getWaves().size();
#endif

#if DEBUG_WAVE_TIMING
        Serial.printf("Wave: interval=%lums period=%lums active=%u\n",
                      (unsigned long)lastWaveIntervalMs,
                      (unsigned long)periodMs,
                      (unsigned)getWaves().size());
#endif

        do {
          nextWaveDueMs += periodMs;
        } while ((int32_t)(now - nextWaveDueMs) >= 0);
      }
    }
  } else {
    nextWaveDueMs = 0;
  }
#endif

#if ENABLE_FALLBACK_WAVES
  if (!g_config.enableBeatWaves && g_config.enableFallbackWaves) {
    nextWaveDueMs = 0;
    // Inject a wave only if we haven't detected any beat for a while.
    // Note: if the music tempo is slower than fallbackMs (e.g. < 75 BPM),
    // this will also inject waves between beats.
    if ((now - lastBeatMs >= g_config.fallbackMs) && (now - lastWaveTime >= g_config.fallbackMs)) {
      if (getWaves().size() < g_config.maxActiveWaves) {
        const uint32_t hue = (uint32_t)random(0, 65536);
        const int16_t hueStartDeg = (int16_t)random(-360, 361);
        const int16_t hueEndDeg = (int16_t)random(-360, 361);
        const int8_t speedCtl = speedControlFromPeriod(g_config.fallbackMs);
        float nose = 1.0f;
        float tail = 1.0f;
        computeWaveWidths(0.0f, &nose, &tail);
        nose = clampf(nose, WAVE_NOSE_MIN, WAVE_NOSE_MAX);
        addWave(hue, speedCtl, nose, tail, false, hueStartDeg, hueEndDeg);
        wavesAdded = true;
      }
      lastWaveIntervalMs = (lastWaveTime > 0) ? (now - lastWaveTime) : 0;
      lastWaveTime = now;
#if ENABLE_WEB_TELEMETRY
      telemetry.lastWaveMs = now;
      telemetry.lastWaveIntervalMs = lastWaveIntervalMs;
      telemetry.activeWaves = (uint32_t)getWaves().size();
#endif

  if (wavesAdded) {
    applyWaveSpacing(WAVE_SPACING_MIX, WAVE_NOSE_MIN, WAVE_NOSE_MAX);
    lastSpacingMs = now;
  }
#if DEBUG_WAVE_TIMING
      Serial.printf("Wave(fallback): interval=%lums fallback=%lums active=%u\n",
                    (unsigned long)lastWaveIntervalMs,
                    (unsigned long)g_config.fallbackMs,
                    (unsigned)getWaves().size());
#endif
    }
  }
#endif

  if (pulseRatio < 0.999f) {
    applyPulseToStrip(leds1, NUM_LEDS1, pulseRatio);
  }
}

void setup() {
  delay(300);
  Serial.begin(115200);

#if (ENABLE_WEB_TELEMETRY || ENABLE_OTA)
  wifiConnected = setupWiFi();
#endif
#if ENABLE_WEB_TELEMETRY
  setupWebServer();
#endif
#if ENABLE_OTA
  if (wifiConnected) {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    if (OTA_PASSWORD[0] != '\0') {
      ArduinoOTA.setPassword(OTA_PASSWORD);
    }
    ArduinoOTA.begin();
    Serial.print("OTA ready: ");
    Serial.print(OTA_HOSTNAME);
    Serial.print(".local (");
    Serial.print(WiFi.localIP());
    Serial.println(")");
  }
#endif

  FastLED.addLeds<NEOPIXEL, DATA_PIN1>(leds1, NUM_LEDS1);
#if ENABLE_HAIR_STRIP
  FastLED.addLeds<NEOPIXEL, DATA_PIN2>(leds2, NUM_LEDS2);
#endif

  // Keep FastLED's global brightness scaler at full.
  // Beat pulsing is handled in the frame renderer.
  FastLED.setBrightness(255);

  fill_solid(leds1, NUM_LEDS1, CRGB::Black);
#if ENABLE_HAIR_STRIP
  fill_solid(leds2, NUM_LEDS2, CRGB::Black);
#endif
  showStrips();

  pinMode(BUTTON_PIN, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT);

  resetWaves();
  setWaveSpeedBaseFps(1000.0f / (float)DELAY_MS);
  normalizeConfig();
  applyAnimationConfig();
  applyBeatConfig();

  // Simple entropy seed (works without ADC wiring).
  randomSeed((uint32_t)micros());

  setupI2S();

  lastWaveTime = millis();
  lastAudioTime = millis();

#if AUDIO_TASK_ENABLE
  if (xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 1, &s_audioTaskHandle, 0) != pdPASS) {
    Serial.println("Audio task create failed");
  }
#endif
}

void loop() {
#if TEST_SOLID_COLOR
  static bool on = false;
  static uint32_t lastToggle = 0;
  const uint32_t nowMs = millis();
  if (nowMs - lastToggle >= 1000) {
    on = !on;
    lastToggle = nowMs;
  }

  const uint16_t count = (TEST_LED_COUNT < NUM_LEDS1) ? TEST_LED_COUNT : NUM_LEDS1;
  const CRGB color = on ? CRGB(255, 255, 255) : CRGB::Black;
  for (uint16_t i = 0; i < count; i++) {
    leds1[i] = color;
  }
  for (uint16_t i = count; i < NUM_LEDS1; i++) {
    leds1[i] = CRGB::Black;
  }
#if ENABLE_HAIR_STRIP
  const uint16_t count2 = NUM_LEDS2;
  for (uint16_t i = 0; i < count2; i++) {
    leds2[i] = color;
  }
  for (uint16_t i = count2; i < NUM_LEDS2; i++) {
    leds2[i] = CRGB::Black;
  }
#endif
  showStrips();
  delay(10);
  return;
#endif
  const uint32_t now = millis();

#if !AUDIO_TASK_ENABLE
  if (now - lastAudioTime >= AUDIO_INTERVAL) {
#if PROFILE_PERF
    const uint32_t t0 = micros();
#endif
    processAudio();
#if PROFILE_PERF
    s_audioAccumUs += (uint32_t)(micros() - t0);
    s_audioCount += 1;
#endif
    lastAudioTime = now;
  }
#endif

  handleButton();

#if PROFILE_PERF
  const uint32_t t1 = micros();
#endif
  runLedAnimation();
#if PROFILE_PERF
  s_animAccumUs += (uint32_t)(micros() - t1);
  s_animCount += 1;
#endif

#if ENABLE_HAIR_STRIP
  updateHairStrip(now);
#endif

#if PROFILE_PERF
  const uint32_t t2 = micros();
#endif
  showStrips();
#if PROFILE_PERF
  s_showAccumUs += (uint32_t)(micros() - t2);
  s_showCount += 1;

  if ((now - s_lastProfileMs) >= PROFILE_INTERVAL_MS) {
    s_lastProfileMs = now;
    const uint32_t audioAvg = s_audioCount ? (uint32_t)(s_audioAccumUs / s_audioCount) : 0;
    const uint32_t animAvg = s_animCount ? (uint32_t)(s_animAccumUs / s_animCount) : 0;
    const uint32_t showAvg = s_showCount ? (uint32_t)(s_showAccumUs / s_showCount) : 0;
    Serial.printf("perf avg (us): audio=%lu anim=%lu show=%lu\n",
                  (unsigned long)audioAvg,
                  (unsigned long)animAvg,
                  (unsigned long)showAvg);
    s_audioAccumUs = 0;
    s_audioCount = 0;
    s_animAccumUs = 0;
    s_animCount = 0;
    s_showAccumUs = 0;
    s_showCount = 0;
  }
#endif
#if ENABLE_WEB_TELEMETRY
  pollWebServer();
#endif
#if (ENABLE_WEB_TELEMETRY || ENABLE_OTA)
#if ENABLE_WIFI_KEEPALIVE
  pollWiFi();
#endif
#endif
#if ENABLE_OTA
  if (wifiConnected) {
    ArduinoOTA.handle();
  }
#endif
  delay(DELAY_MS);
  yield();
}
