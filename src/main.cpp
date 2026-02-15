#include <Arduino.h>
#include <FastLED.h>
#include "animation_engine.h"
#include "audio_scheduler.h"
#include "audio_processor.h"
#include "controls.h"
#include "elsa_config.h"
#include "hair_strip.h"
#include "networking.h"
#include "runtime_config.h"
#include "telemetry_state.h"
#include "wave_position.h"
#include "web_telemetry.h"
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
#endif

#if ENABLE_WEB_TELEMETRY
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
#endif

static inline void showStrips() {
  FastLED.show();
}

void setup() {
  delay(300);
  Serial.begin(115200);

#if (ENABLE_WEB_TELEMETRY || ENABLE_OTA)
  setupWiFi();
#endif
#if ENABLE_WEB_TELEMETRY
  webTelemetryInit(&g_config, &telemetry, leds1, NUM_LEDS1, wifiConnectedFlag());
  webTelemetrySetup();
#endif
#if ENABLE_OTA
  setupOta();
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

  setupButton();

#if ENABLE_WEB_TELEMETRY
  animationEngineInit(leds1, NUM_LEDS1, &telemetry);
#else
  animationEngineInit(leds1, NUM_LEDS1, nullptr);
#endif

  resetWaves();
  setWaveSpeedBaseFps(1000.0f / (float)DELAY_MS);
  animationEngineReset(millis());
  normalizeConfig();
  applyAnimationConfig();
  applyBeatConfig();

  // Simple entropy seed (works without ADC wiring).
  randomSeed((uint32_t)micros());

  setupI2S();
  audioSchedulerInit();
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

#if PROFILE_PERF
  uint32_t audioUs = 0;
  if (audioSchedulerRun(now, &audioUs)) {
    s_audioAccumUs += audioUs;
    s_audioCount += 1;
  }
#else
  audioSchedulerRun(now, nullptr);
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
  updateHairStrip(now, leds2, NUM_LEDS2);
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
  webTelemetryPoll();
#endif
#if (ENABLE_WEB_TELEMETRY || ENABLE_OTA)
  pollWiFi();
#endif
#if ENABLE_OTA
  handleOta();
#endif
  delay(DELAY_MS);
  yield();
}
