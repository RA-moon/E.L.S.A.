#include "web_telemetry.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <vector>

#include "animation_manager.h"
#include "audio_processor.h"
#include "runtime_config.h"
#include "telemetry_state.h"
#include "web_telemetry_page.h"

static RuntimeConfig* s_config = nullptr;
static BeatTelemetry* s_telemetry = nullptr;
static CRGB* s_leds = nullptr;
static uint16_t s_ledCount = 0;
static bool* s_wifiConnected = nullptr;

void webTelemetryInit(RuntimeConfig* config,
                      BeatTelemetry* telemetry,
                      CRGB* leds,
                      uint16_t ledCount,
                      bool* wifiConnected) {
  s_config = config;
  s_telemetry = telemetry;
  s_leds = leds;
  s_ledCount = ledCount;
  s_wifiConnected = wifiConnected;
}

#if ENABLE_WEB_TELEMETRY
static WebServer server(WEB_SERVER_PORT);
static uint32_t s_lastFrameSendMs = 0;

static inline bool ready() {
  return s_config && s_telemetry && s_leds && s_ledCount > 0;
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

static void handleRoot() {
  server.send_P(200, "text/html", kWebTelemetryHtml);
}

static bool updateConfigFromRequest() {
  if (!ready()) return false;
  RuntimeConfig& cfg = *s_config;
  bool changed = false;
  long value = 0;
  bool boolValue = false;

  if (server.hasArg("brightness") && parseLongArg(server.arg("brightness"), &value)) {
    cfg.brightness = clampU8((int)value, 0, 255);
    changed = true;
  }
  if (server.hasArg("beatMin") && parseLongArg(server.arg("beatMin"), &value)) {
    cfg.beatDecayMinMs = clampU16(value, 50, 5000);
    changed = true;
  }
  if (server.hasArg("beatMax") && parseLongArg(server.arg("beatMax"), &value)) {
    cfg.beatDecayMaxMs = clampU16(value, 50, 10000);
    changed = true;
  }
  if (server.hasArg("pulseLeadMs") && parseLongArg(server.arg("pulseLeadMs"), &value)) {
    if (value < -250) value = -250;
    if (value > 250) value = 250;
    cfg.pulseLeadMs = (int16_t)value;
    changed = true;
  }
  if (server.hasArg("fallbackMs") && parseLongArg(server.arg("fallbackMs"), &value)) {
    cfg.fallbackMs = clampU16(value, 0, 10000);
    changed = true;
  }
  if (server.hasArg("maxWaves") && parseLongArg(server.arg("maxWaves"), &value)) {
    cfg.maxActiveWaves = clampU8((int)value, 1, 100);
    changed = true;
  }
  if (server.hasArg("beatWaves") && parseBoolArg(server.arg("beatWaves"), &boolValue)) {
    cfg.enableBeatWaves = boolValue;
    changed = true;
  }
  if (server.hasArg("fallbackWaves") && parseBoolArg(server.arg("fallbackWaves"), &boolValue)) {
    cfg.enableFallbackWaves = boolValue;
    changed = true;
  }
  if (server.hasArg("energyEmaAlpha") && parseFloatArg(server.arg("energyEmaAlpha"), &cfg.energyEmaAlpha)) {
    changed = true;
  }
  if (server.hasArg("fluxEmaAlpha") && parseFloatArg(server.arg("fluxEmaAlpha"), &cfg.fluxEmaAlpha)) {
    changed = true;
  }
  if (server.hasArg("fluxThreshold") && parseFloatArg(server.arg("fluxThreshold"), &cfg.fluxThreshold)) {
    changed = true;
  }
  if (server.hasArg("fluxRiseFactor") && parseFloatArg(server.arg("fluxRiseFactor"), &cfg.fluxRiseFactor)) {
    changed = true;
  }
  if (server.hasArg("minBeatIntervalMs") && parseLongArg(server.arg("minBeatIntervalMs"), &value)) {
    cfg.minBeatIntervalMs = clampU16(value, 80, 1000);
    changed = true;
  }
  if (server.hasArg("avgBeatMinMs") && parseLongArg(server.arg("avgBeatMinMs"), &value)) {
    cfg.avgBeatMinMs = clampU16(value, 430, 800);
    changed = true;
  }
  if (server.hasArg("avgBeatMaxMs") && parseLongArg(server.arg("avgBeatMaxMs"), &value)) {
    cfg.avgBeatMaxMs = clampU16(value, 430, 800);
    changed = true;
  }
  if (server.hasArg("mode")) {
    const String mode = server.arg("mode");
    if (mode == "auto" || mode == "1") {
      cfg.animationAuto = true;
      changed = true;
    } else if (mode == "fixed" || mode == "manual" || mode == "0") {
      cfg.animationAuto = false;
      changed = true;
    }
  }
  if (server.hasArg("anim") && parseLongArg(server.arg("anim"), &value)) {
    cfg.animationIndex = (int)value;
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
  RuntimeConfig& cfg = *s_config;
  const int animCount = getAnimationCount();
  String json;
  json.reserve(640);
  json += "{";
  json += "\"brightness\":" + String(cfg.brightness);
  json += ",\"beatDecayMinMs\":" + String(cfg.beatDecayMinMs);
  json += ",\"beatDecayMaxMs\":" + String(cfg.beatDecayMaxMs);
  json += ",\"pulseLeadMs\":" + String(cfg.pulseLeadMs);
  json += ",\"fallbackMs\":" + String(cfg.fallbackMs);
  json += ",\"maxActiveWaves\":" + String(cfg.maxActiveWaves);
  json += ",\"enableBeatWaves\":" + String(cfg.enableBeatWaves ? 1 : 0);
  json += ",\"enableFallbackWaves\":" + String(cfg.enableFallbackWaves ? 1 : 0);
  json += ",\"beat\":{\"energyEmaAlpha\":" + String(cfg.energyEmaAlpha, 3);
  json += ",\"fluxEmaAlpha\":" + String(cfg.fluxEmaAlpha, 3);
  json += ",\"fluxThreshold\":" + String(cfg.fluxThreshold, 3);
  json += ",\"fluxRiseFactor\":" + String(cfg.fluxRiseFactor, 3);
  json += ",\"minBeatIntervalMs\":" + String(cfg.minBeatIntervalMs);
  json += ",\"avgBeatMinMs\":" + String(cfg.avgBeatMinMs);
  json += ",\"avgBeatMaxMs\":" + String(cfg.avgBeatMaxMs);
  json += "}";
  json += ",\"animation\":{\"mode\":\"";
  json += (cfg.animationAuto ? "auto" : "fixed");
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
  if (!ready()) {
    server.send(500, "text/plain", "config not ready");
    return;
  }
  updateConfigFromRequest();
  const String json = buildConfigJson();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

static void handleStatus() {
  if (!ready()) {
    server.send(500, "text/plain", "status not ready");
    return;
  }
  const uint32_t now = millis();
  const uint32_t lastBeat = s_telemetry->lastBeatMs;
  const uint32_t age = (lastBeat > 0) ? (now - lastBeat) : 0;
  const uint32_t lastWave = s_telemetry->lastWaveMs;
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
    (unsigned long)s_telemetry->beatCount,
    (unsigned long)lastBeat,
    (unsigned long)age,
    (unsigned)s_ledCount,
    (unsigned)(s_ledCount * 3),
    s_telemetry->lastBeatStrength,
    s_telemetry->avgBeatIntervalMs,
    s_telemetry->bpm,
    (unsigned long)lastWave,
    (unsigned long)waveAge,
    (unsigned long)s_telemetry->lastWaveIntervalMs,
    (unsigned long)s_telemetry->wavePeriodMs,
    (unsigned long)s_telemetry->nextWaveInMs,
    (unsigned long)s_telemetry->activeWaves,
    s_telemetry->animationIndex,
    s_telemetry->animationName ? s_telemetry->animationName : "unknown",
    (unsigned)s_config->brightness,
    s_telemetry->baseBrightnessRatio,
    s_telemetry->pulseRatio,
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
  if (!ready()) {
    server.send(500, "text/plain", "frame not ready");
    return;
  }
  const uint32_t now = millis();
  if (FRAME_MIN_INTERVAL_MS > 0 && (now - s_lastFrameSendMs) < FRAME_MIN_INTERVAL_MS) {
    server.send(204, "text/plain", "");
    return;
  }

  s_lastFrameSendMs = now;
  static std::vector<uint8_t> frame;
  const size_t needed = (size_t)s_ledCount * 3;
  if (frame.size() != needed) {
    frame.assign(needed, 0);
  }

  for (uint16_t i = 0; i < s_ledCount; i++) {
    const CRGB c = s_leds[i];
    frame[(i * 3) + 0] = c.r;
    frame[(i * 3) + 1] = c.g;
    frame[(i * 3) + 2] = c.b;
  }

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength((int)needed);
  server.send(200, "application/octet-stream", "");
  server.sendContent((const char*)frame.data(), (int)needed);
}

void webTelemetrySetup() {
  if (!s_wifiConnected || !*s_wifiConnected) return;
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

void webTelemetryPoll() {
  if (!s_wifiConnected || !*s_wifiConnected) return;
  server.handleClient();
}
#endif
