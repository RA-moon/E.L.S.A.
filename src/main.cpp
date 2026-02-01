#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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
#define ENABLE_HAIR_STRIP  0
#define DATA_PIN2          2
#define NUM_LEDS2          44

#define BRIGHTNESS1       80   // baseline (0..255)
#define DELAY_MS          10
#define NO_BEAT_FALLBACK_MS 800
#define AUDIO_INTERVAL    15
#define MAX_ACTIVE_WAVES  20

// === Test mode ===
// Set to 1 to blink white on the first TEST_LED_COUNT LEDs (matching your Arduino IDE test).
#define TEST_SOLID_COLOR  0
#define TEST_LED_COUNT    30

// === Web telemetry (beat/pattern output) ===
#define ENABLE_WEB_TELEMETRY  1
#define WEB_SERVER_PORT       80
#define WIFI_CONNECT_TIMEOUT_MS 12000
// Minimum gap between /frame responses (ms). Increase if animation stutters.
#define FRAME_MIN_INTERVAL_MS 12

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Beat-synced brightness envelope:
// - On beat: brightness = 255
// - Then decays to BRIGHTNESS1 over the *average* beat time
#define BEAT_DECAY_MIN_MS   160
#define BEAT_DECAY_MAX_MS  1500
#define BEAT_DECAY_EASE_OUT   1   // 1 = quadratic ease-out, 0 = linear

// Waves are triggered on detected beats. If no beats are detected for a while,
// the fallback timer will still inject occasional waves so the strip doesn't go idle.
#define ENABLE_BEAT_WAVES     1
#define ENABLE_FALLBACK_WAVES 1

// Serial debug print on every beat
#define DEBUG_BEAT_TIMING     0

// Performance profiling (averages printed to Serial).
#define PROFILE_PERF          0
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

Adafruit_NeoPixel strip1(NUM_LEDS1, DATA_PIN1, NEO_GRB + NEO_KHZ800);

#if ENABLE_HAIR_STRIP
Adafruit_NeoPixel strip2(NUM_LEDS2, DATA_PIN2, NEO_GRB + NEO_KHZ800);
#endif

static uint32_t lastWaveTime = 0;
static uint32_t lastAudioTime = 0;

// Beat envelope state
static uint32_t lastBeatMs = 0;

struct RuntimeConfig {
  uint8_t brightness;
  uint16_t beatDecayMinMs;
  uint16_t beatDecayMaxMs;
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
  120,
  180,
  2000
};

#if ENABLE_WEB_TELEMETRY
struct BeatTelemetry {
  uint32_t beatCount;
  uint32_t lastBeatMs;
  float lastBeatStrength;
  float avgBeatIntervalMs;
  float bpm;
  int animationIndex;
  const char* animationName;
};

static BeatTelemetry telemetry = {
  0,
  0,
  0.0f,
  0.0f,
  0.0f,
  0,
  "unknown"
};

static WebServer server(WEB_SERVER_PORT);
static bool wifiConnected = false;
static uint32_t lastFrameSendMs = 0;

static void handleRoot();
static void handleStatus();
static void handleFrame();
static void handleConfig();
static bool setupWiFi();
static void setupWebServer();
static void pollWebServer();
#endif

static inline void showStrips() {
  strip1.show();
#if ENABLE_HAIR_STRIP
  strip2.show();
#endif
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
  g_config.avgBeatMinMs = clampU16((long)g_config.avgBeatMinMs, 100, 1200);
  g_config.avgBeatMaxMs = clampU16((long)g_config.avgBeatMaxMs, 500, 5000);
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

static inline float beatEnvelope(float beatPeriodMs, uint32_t nowMs) {
  if (lastBeatMs == 0) return 0.0f;

  const uint32_t dt = nowMs - lastBeatMs;
  if (dt >= (uint32_t)beatPeriodMs) return 0.0f;

  float e = 1.0f - ((float)dt / beatPeriodMs); // 1..0
#if BEAT_DECAY_EASE_OUT
  e *= e;
#endif
  return clamp01(e);
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
    </div>
    <canvas id="strip" width="120" height="1"></canvas>
    <form id="config-form">
      <div class="row">
        <div class="col">
          <label for="brightness">Brightness</label>
          <input type="range" id="brightness" name="brightness" min="0" max="255" step="1" />
        </div>
        <div class="col">
          <div class="small">value</div>
          <div id="brightness-value">--</div>
        </div>
      </div>
      <div class="row">
        <label for="beat-min">Beat decay min (ms)</label>
        <input type="number" id="beat-min" name="beatMin" min="50" max="5000" step="10" />
        <label for="beat-max">Beat decay max (ms)</label>
        <input type="number" id="beat-max" name="beatMax" min="50" max="10000" step="10" />
      </div>
      <div class="row">
        <label for="fallback-ms">No-beat fallback (ms)</label>
        <input type="number" id="fallback-ms" name="fallbackMs" min="0" max="10000" step="50" />
        <label for="max-waves">Max waves</label>
        <input type="number" id="max-waves" name="maxWaves" min="1" max="100" step="1" />
      </div>
    <div class="row">
      <label for="mode">Animation mode</label>
      <select id="mode" name="mode">
        <option value="auto">auto</option>
        <option value="fixed">fixed</option>
      </select>
      <label for="anim">Animation</label>
      <select id="anim" name="anim"></select>
      <button type="button" id="anim-toggle">Auto: ON</button>
    </div>
      <div class="row">
        <label><input type="checkbox" id="beat-waves" /> Beat waves</label>
        <label><input type="checkbox" id="fallback-waves" /> Fallback waves</label>
      </div>
      <div class="row">
        <label for="energy-ema">Energy EMA</label>
        <input type="number" id="energy-ema" name="energyEmaAlpha" min="0.01" max="0.50" step="0.01" />
        <label for="flux-ema">Flux EMA</label>
        <input type="number" id="flux-ema" name="fluxEmaAlpha" min="0.01" max="0.60" step="0.01" />
      </div>
      <div class="row">
        <label for="flux-threshold">Flux threshold</label>
        <input type="number" id="flux-threshold" name="fluxThreshold" min="1.1" max="4.0" step="0.05" />
        <label for="flux-rise">Flux rise</label>
        <input type="number" id="flux-rise" name="fluxRiseFactor" min="0.02" max="0.60" step="0.01" />
      </div>
      <div class="row">
        <label for="min-interval">Min beat interval (ms)</label>
        <input type="number" id="min-interval" name="minBeatIntervalMs" min="80" max="1000" step="10" />
        <label for="avg-min">Avg beat min (ms)</label>
        <input type="number" id="avg-min" name="avgBeatMinMs" min="100" max="1200" step="10" />
        <label for="avg-max">Avg beat max (ms)</label>
        <input type="number" id="avg-max" name="avgBeatMaxMs" min="500" max="5000" step="50" />
      </div>
      <div class="row">
        <button type="submit">Apply</button>
        <div class="small" id="config-status"></div>
      </div>
    </form>
    <pre id="payload">waiting...</pre>
    <script>
      const payloadEl = document.getElementById('payload');
      const fpsEl = document.getElementById('fps');
      const strip = document.getElementById('strip');
      const ctx = strip.getContext('2d');
      const configForm = document.getElementById('config-form');
      const brightnessInput = document.getElementById('brightness');
      const brightnessValue = document.getElementById('brightness-value');
      const beatMinInput = document.getElementById('beat-min');
      const beatMaxInput = document.getElementById('beat-max');
      const fallbackInput = document.getElementById('fallback-ms');
      const maxWavesInput = document.getElementById('max-waves');
      const modeSelect = document.getElementById('mode');
      const animSelect = document.getElementById('anim');
      const animToggle = document.getElementById('anim-toggle');
      const beatWavesInput = document.getElementById('beat-waves');
      const fallbackWavesInput = document.getElementById('fallback-waves');
      const energyEmaInput = document.getElementById('energy-ema');
      const fluxEmaInput = document.getElementById('flux-ema');
      const fluxThresholdInput = document.getElementById('flux-threshold');
      const fluxRiseInput = document.getElementById('flux-rise');
      const minIntervalInput = document.getElementById('min-interval');
      const avgMinInput = document.getElementById('avg-min');
      const avgMaxInput = document.getElementById('avg-max');
      const configStatus = document.getElementById('config-status');

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

      function updateBrightnessLabel() {
        brightnessValue.textContent = brightnessInput.value;
      }

      function setConfigUI(data) {
        if (!data) return;
        brightnessInput.value = data.brightness ?? brightnessInput.value;
        updateBrightnessLabel();
        if (data.beatDecayMinMs !== undefined) beatMinInput.value = data.beatDecayMinMs;
        if (data.beatDecayMaxMs !== undefined) beatMaxInput.value = data.beatDecayMaxMs;
        if (data.fallbackMs !== undefined) fallbackInput.value = data.fallbackMs;
        if (data.maxActiveWaves !== undefined) maxWavesInput.value = data.maxActiveWaves;
        if (data.enableBeatWaves !== undefined) beatWavesInput.checked = !!data.enableBeatWaves;
        if (data.enableFallbackWaves !== undefined) fallbackWavesInput.checked = !!data.enableFallbackWaves;

        if (data.animation) {
          if (data.animation.mode) modeSelect.value = data.animation.mode;
          const list = data.animations || [];
          animSelect.innerHTML = '';
          list.forEach((name, idx) => {
            const opt = document.createElement('option');
            opt.value = idx;
            opt.textContent = name;
            animSelect.appendChild(opt);
          });
          if (data.animation.index !== undefined) animSelect.value = data.animation.index;
          animSelect.disabled = (modeSelect.value === 'auto');
        }
        if (data.beat) {
          if (data.beat.energyEmaAlpha !== undefined) energyEmaInput.value = data.beat.energyEmaAlpha;
          if (data.beat.fluxEmaAlpha !== undefined) fluxEmaInput.value = data.beat.fluxEmaAlpha;
          if (data.beat.fluxThreshold !== undefined) fluxThresholdInput.value = data.beat.fluxThreshold;
          if (data.beat.fluxRiseFactor !== undefined) fluxRiseInput.value = data.beat.fluxRiseFactor;
          if (data.beat.minBeatIntervalMs !== undefined) minIntervalInput.value = data.beat.minBeatIntervalMs;
          if (data.beat.avgBeatMinMs !== undefined) avgMinInput.value = data.beat.avgBeatMinMs;
          if (data.beat.avgBeatMaxMs !== undefined) avgMaxInput.value = data.beat.avgBeatMaxMs;
        }
        animToggle.textContent = (modeSelect.value === 'auto') ? 'Auto: ON' : 'Auto: OFF';
      }

      async function fetchConfig() {
        try {
          const res = await fetch('/config', { cache: 'no-store' });
          const data = await res.json();
          setConfigUI(data);
        } catch (err) {
          configStatus.textContent = 'config error';
        }
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

      brightnessInput.addEventListener('input', updateBrightnessLabel);
      modeSelect.addEventListener('change', () => {
        animSelect.disabled = (modeSelect.value === 'auto');
        animToggle.textContent = (modeSelect.value === 'auto') ? 'Auto: ON' : 'Auto: OFF';
      });

      async function applyConfig() {
        const params = new URLSearchParams(new FormData(configForm));
        params.set('beatWaves', beatWavesInput.checked ? '1' : '0');
        params.set('fallbackWaves', fallbackWavesInput.checked ? '1' : '0');
        const url = '/config?' + params.toString();
        configStatus.textContent = 'updating...';
        try {
          const res = await fetch(url, { cache: 'no-store' });
          const data = await res.json();
          setConfigUI(data);
          configStatus.textContent = 'updated';
        } catch (err) {
          configStatus.textContent = 'update failed';
        }
      }

      configForm.addEventListener('submit', async (event) => {
        event.preventDefault();
        applyConfig();
      });

      let tapTimer = null;
      let lastTap = 0;
      animToggle.addEventListener('click', () => {
        const now = Date.now();
        if (now - lastTap < 350) {
          lastTap = 0;
          if (tapTimer) {
            clearTimeout(tapTimer);
            tapTimer = null;
          }
          modeSelect.value = (modeSelect.value === 'auto') ? 'fixed' : 'auto';
          animSelect.disabled = (modeSelect.value === 'auto');
          animToggle.textContent = (modeSelect.value === 'auto') ? 'Auto: ON' : 'Auto: OFF';
          applyConfig();
          return;
        }

        lastTap = now;
        tapTimer = setTimeout(() => {
          tapTimer = null;
          if (modeSelect.value !== 'auto' && animSelect.options.length > 0) {
            const next = (parseInt(animSelect.value || '0', 10) + 1) % animSelect.options.length;
            animSelect.value = String(next);
            applyConfig();
          }
        }, 350);
      });

      fetchStatus();
      fetchConfig();
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
    g_config.avgBeatMinMs = clampU16(value, 100, 1200);
    changed = true;
  }
  if (server.hasArg("avgBeatMaxMs") && parseLongArg(server.arg("avgBeatMaxMs"), &value)) {
    g_config.avgBeatMaxMs = clampU16(value, 500, 5000);
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

  AudioTelemetry audio = {};
  getAudioTelemetry(&audio);

  char json[900];
  const int n = snprintf(
    json,
    sizeof(json),
    "{\"uptimeMs\":%lu,\"beatCount\":%lu,\"lastBeatMs\":%lu,\"lastBeatAgeMs\":%lu,"
    "\"ledCount\":%u,\"frameBytes\":%u,"
    "\"lastBeatStrength\":%.3f,\"avgBeatIntervalMs\":%.1f,\"bpm\":%.1f,"
    "\"animation\":{\"index\":%d,\"name\":\"%s\"},"
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
    telemetry.animationIndex,
    telemetry.animationName ? telemetry.animationName : "unknown",
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
    const uint32_t c = strip1.getPixelColor(i);
    frame[(i * 3) + 0] = (uint8_t)((c >> 16) & 0xFF); // R
    frame[(i * 3) + 1] = (uint8_t)((c >> 8) & 0xFF);  // G
    frame[(i * 3) + 2] = (uint8_t)(c & 0xFF);         // B
  }

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(NUM_LEDS1 * 3);
  server.send(200, "application/octet-stream", "");
  server.sendContent((const char*)frame, NUM_LEDS1 * 3);
}

static bool setupWiFi() {
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
}

static void setupWebServer() {
  if (!wifiConnected) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/frame", HTTP_GET, handleFrame);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/config", HTTP_POST, handleConfig);
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

void runLedAnimation() {
  const uint32_t now = millis();

#if ENABLE_BEAT_WAVES
  float beatStrength = 0.0f;
  bool beatEvent = false;
  if (g_config.enableBeatWaves) {
    beatEvent = consumeBeat(&beatStrength);
    if (beatEvent) {
      lastBeatMs = now;

      // Suppress the fallback timer right after a beat.
      lastWaveTime = now;

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
  }
#else
  const bool beatEvent = false;
  const float beatStrength = 0.0f;
#endif

  // Use the tempo estimate from the audio module.
  float beatPeriodMs = getAverageBeatIntervalMs();
  if (beatPeriodMs < (float)g_config.beatDecayMinMs) beatPeriodMs = (float)g_config.beatDecayMinMs;
  if (beatPeriodMs > (float)g_config.beatDecayMaxMs) beatPeriodMs = (float)g_config.beatDecayMaxMs;

  const float env = beatEnvelope(beatPeriodMs, now);

  // Brightness for this frame: 255 on beat -> baseline at the end of the beat period.
  int frameBrightness = g_config.brightness + (int)lroundf((255.0f - (float)g_config.brightness) * env);
  frameBrightness = constrain(frameBrightness, 0, 255);

  updateAnimationSwitch();
  const auto& frames = getCurrentAnimationFrames();

#if ENABLE_WEB_TELEMETRY
  telemetry.avgBeatIntervalMs = getAverageBeatIntervalMs();
  telemetry.bpm = getAverageBpm();
  telemetry.animationIndex = getCurrentAnimationIndex();
  telemetry.animationName = getCurrentAnimationName();
#endif

  // Tell the wave engine how many frames the current animation has.
  setWaveFrameCount((int)frames.size());

  strip1.clear();

  updateWaves();
  const auto& waves = getWaves();

  for (const auto& wave : waves) {
    auto frame = getInterpolatedFrame(
      frames,
      wave.center,
      wave.hue,
      wave.tailWidth,
      wave.noseWidth,
      frameBrightness,
      wave.reverse
    );

    for (const auto& f : frame) {
      if (f.ledIndex >= 0 && f.ledIndex < NUM_LEDS1) {
        strip1.setPixelColor((uint16_t)f.ledIndex, f.color);
      }
    }
  }

#if ENABLE_BEAT_WAVES
  // Beat-triggered wave injection.
  if (beatEvent) {
    if (getWaves().size() < g_config.maxActiveWaves) {
      const uint32_t hue = (uint32_t)random(0, 65536);
      const int8_t speedCtl = (int8_t)constrain((int)(beatStrength * 25.0f) - 5, -10, 10);
      const float nose = 0.8f + (beatStrength * 2.5f);
      const float tail = 1.5f + (beatStrength * 4.0f);
      const bool reverse = (random(0, 100) < 25);
      addWave(hue, speedCtl, nose, tail, reverse);
    }
  }
#endif

#if ENABLE_FALLBACK_WAVES
  if (g_config.enableFallbackWaves) {
    // Inject a wave only if we haven't detected any beat for a while.
    // Note: if the music tempo is slower than fallbackMs (e.g. < 75 BPM),
    // this will also inject waves between beats.
    if ((now - lastBeatMs >= g_config.fallbackMs) && (now - lastWaveTime >= g_config.fallbackMs)) {
      if (getWaves().size() < g_config.maxActiveWaves) {
        addWave((uint32_t)random(0, 65536), 0, 1.0f, 2.0f);
      }
      lastWaveTime = now;
    }
  }
#endif
}

void setup() {
  delay(300);
  Serial.begin(115200);

#if ENABLE_WEB_TELEMETRY
  wifiConnected = setupWiFi();
  setupWebServer();
#endif

  strip1.begin();
#if ENABLE_HAIR_STRIP
  strip2.begin();
#endif

  // Keep NeoPixel's internal brightness scaler at full.
  // Beat pulsing is handled by the per-frame brightness parameter.
  strip1.setBrightness(255);
#if ENABLE_HAIR_STRIP
  strip2.setBrightness(255);
#endif

  strip1.clear();
#if ENABLE_HAIR_STRIP
  strip2.clear();
#endif
  showStrips();

  resetWaves();
  normalizeConfig();
  applyAnimationConfig();
  applyBeatConfig();

  // Simple entropy seed (works without ADC wiring).
  randomSeed((uint32_t)micros());

  setupI2S();

  lastWaveTime = millis();
  lastAudioTime = millis();
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
  const uint32_t color = on ? strip1.Color(255, 255, 255) : 0;
  for (uint16_t i = 0; i < count; i++) {
    strip1.setPixelColor(i, color);
  }
  for (uint16_t i = count; i < NUM_LEDS1; i++) {
    strip1.setPixelColor(i, 0);
  }
  showStrips();
  delay(10);
  return;
#endif
  const uint32_t now = millis();

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

#if PROFILE_PERF
  const uint32_t t1 = micros();
#endif
  runLedAnimation();
#if PROFILE_PERF
  s_animAccumUs += (uint32_t)(micros() - t1);
  s_animCount += 1;
#endif

#if ENABLE_HAIR_STRIP
  // Hair rendering will be re-enabled later.
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
  delay(DELAY_MS);
  yield();
}
