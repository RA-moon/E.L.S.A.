#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <stdio.h>

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

// ESP32-S3 tuning (override if you want different trade-offs).
#if defined(ARDUINO_ESP32S3_DEV) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ESP32S3)
#define AUDIO_SAMPLE_RATE_HZ 48000
#define AUDIO_FFT_SAMPLES    1024
#endif

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

Adafruit_NeoPixel strip1(NUM_LEDS1, DATA_PIN1, NEO_GRB + NEO_KHZ800);

#if ENABLE_HAIR_STRIP
Adafruit_NeoPixel strip2(NUM_LEDS2, DATA_PIN2, NEO_GRB + NEO_KHZ800);
#endif

static uint32_t lastWaveTime = 0;
static uint32_t lastAudioTime = 0;

// Beat envelope state
static uint32_t lastBeatMs = 0;

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
    <pre id="payload">waiting...</pre>
    <script>
      const payloadEl = document.getElementById('payload');
      const fpsEl = document.getElementById('fps');
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
  const bool beatEvent = consumeBeat(&beatStrength);
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
#else
  const bool beatEvent = false;
  const float beatStrength = 0.0f;
#endif

  // Use the tempo estimate from the audio module.
  float beatPeriodMs = getAverageBeatIntervalMs();
  if (beatPeriodMs < (float)BEAT_DECAY_MIN_MS) beatPeriodMs = (float)BEAT_DECAY_MIN_MS;
  if (beatPeriodMs > (float)BEAT_DECAY_MAX_MS) beatPeriodMs = (float)BEAT_DECAY_MAX_MS;

  const float env = beatEnvelope(beatPeriodMs, now);

  // Brightness for this frame: 255 on beat -> BRIGHTNESS1 at the end of the beat period.
  int frameBrightness = BRIGHTNESS1 + (int)lroundf((255.0f - (float)BRIGHTNESS1) * env);
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
    if (getWaves().size() < MAX_ACTIVE_WAVES) {
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
  // Inject a wave only if we haven't detected any beat for a while.
  // Note: if the music tempo is slower than NO_BEAT_FALLBACK_MS (e.g. < 75 BPM),
  // this will also inject waves between beats.
  if ((now - lastBeatMs >= NO_BEAT_FALLBACK_MS) && (now - lastWaveTime >= NO_BEAT_FALLBACK_MS)) {
    if (getWaves().size() < MAX_ACTIVE_WAVES) {
      addWave((uint32_t)random(0, 65536), 0, 1.0f, 2.0f);
    }
    lastWaveTime = now;
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
    processAudio();
    lastAudioTime = now;
  }

  runLedAnimation();

#if ENABLE_HAIR_STRIP
  // Hair rendering will be re-enabled later.
#endif

  showStrips();
#if ENABLE_WEB_TELEMETRY
  pollWebServer();
#endif
  delay(DELAY_MS);
  yield();
}
