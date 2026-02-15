#include "animation_engine.h"

#include <Arduino.h>
#include <FastLED.h>
#include <math.h>

#include "animation_manager.h"
#include "audio_processor.h"
#include "elsa_config.h"
#include "frame_interpolation.h"
#include "runtime_config.h"
#include "telemetry_state.h"
#include "wave_position.h"

static CRGB* s_leds = nullptr;
static uint16_t s_ledCount = 0;
static BeatTelemetry* s_telemetry = nullptr;

static uint32_t s_lastWaveTime = 0;
static uint32_t s_lastWaveIntervalMs = 0;
static uint32_t s_lastWavePeriodMs = 0;
static uint32_t s_nextWaveDueMs = 0;
static float s_smoothedBeatPeriodMs = 0.0f;

// Beat envelope state
static uint32_t s_lastBeatMs = 0;
static float s_lastBeatStrength = 0.7f;
static uint32_t s_lastBeatIntervalMs = 0;

// Startup snapshot (for fade-to-startup behavior).
static bool s_startupSnapshotTaken = false;
static float s_startupAvgBeatMs = 500.0f;
static float s_startupBeatStrength = 0.7f;
static float s_startupLastBeatIntervalMs = 0.0f;

// Global brightness pulse envelope (applied after rendering).
static constexpr float kBrightnessMinRatio = 0.30f;
static constexpr float kBrightnessMaxRatio = 1.00f;

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
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
  if (s_lastBeatMs == 0 || beatPeriodMs <= 1.0f) return kBrightnessMaxRatio;

  const uint32_t dt = nowMs - s_lastBeatMs;
  if (dt >= (uint32_t)beatPeriodMs) return kBrightnessMinRatio;

  float e = 1.0f - ((float)dt / beatPeriodMs); // 1..0
#if BEAT_DECAY_EASE_OUT
  e *= e;
#endif
  const float ratio = kBrightnessMinRatio + ((kBrightnessMaxRatio - kBrightnessMinRatio) * e);
  return clampf(ratio, kBrightnessMinRatio, kBrightnessMaxRatio);
}

static inline void applyPulseToStrip(CRGB* leds, uint16_t count, float ratio) {
  if (!leds || count == 0) return;
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

void animationEngineInit(CRGB* leds, uint16_t ledCount, BeatTelemetry* telemetry) {
  s_leds = leds;
  s_ledCount = ledCount;
  s_telemetry = telemetry;
}

void animationEngineReset(uint32_t nowMs) {
  s_lastWaveTime = nowMs;
  s_lastWaveIntervalMs = 0;
  s_lastWavePeriodMs = 0;
  s_nextWaveDueMs = 0;
  s_smoothedBeatPeriodMs = 0.0f;
  s_lastBeatMs = 0;
  s_lastBeatStrength = 0.7f;
  s_lastBeatIntervalMs = 0;
  s_startupSnapshotTaken = false;
}

void runLedAnimation() {
  if (!s_leds || s_ledCount == 0) return;
  const uint32_t now = millis();

  if (!s_startupSnapshotTaken) {
    s_startupSnapshotTaken = true;
    s_startupAvgBeatMs = getAverageBeatIntervalMs();
    s_startupBeatStrength = s_lastBeatStrength;
    s_startupLastBeatIntervalMs = (float)s_lastBeatIntervalMs;
  }

#if ENABLE_BEAT_WAVES
  float beatStrength = 0.0f;
  if (consumeBeat(&beatStrength)) {
    if (s_lastBeatMs > 0) {
      s_lastBeatIntervalMs = now - s_lastBeatMs;
    }
    s_lastBeatMs = now;
    s_lastBeatStrength = beatStrength;

#if ENABLE_WEB_TELEMETRY
    if (s_telemetry) {
      s_telemetry->beatCount += 1;
      s_telemetry->lastBeatMs = now;
      s_telemetry->lastBeatStrength = beatStrength;
    }
#endif

#if DEBUG_BEAT_TIMING
    Serial.printf("Beat: avg=%.0fms (%.1f BPM) strength=%.2f\n",
                  getAverageBeatIntervalMs(), getAverageBpm(), beatStrength);
#endif
  }
#endif

  float fadeToStartup = 0.0f;
#if ENABLE_FADE_TO_STARTUP
  if (s_lastBeatMs > 0) {
    const uint32_t idleMs = now - s_lastBeatMs;
    if (idleMs > FADE_TO_STARTUP_IDLE_MS) {
      const uint32_t overMs = idleMs - FADE_TO_STARTUP_IDLE_MS;
      const float denom = (FADE_TO_STARTUP_DURATION_MS > 0)
        ? (float)FADE_TO_STARTUP_DURATION_MS
        : 1.0f;
      fadeToStartup = clamp01((float)overMs / denom);
    }
  }
#endif

  // Use the tempo estimate from the audio module.
  float beatPeriodMs = getAverageBeatIntervalMs();
  float lastBeatIntervalMsF = (float)s_lastBeatIntervalMs;
  float beatStrengthForWave = s_lastBeatStrength;
  if (fadeToStartup > 0.0f) {
    beatPeriodMs = lerpf(beatPeriodMs, s_startupAvgBeatMs, fadeToStartup);
    lastBeatIntervalMsF = lerpf(lastBeatIntervalMsF, s_startupLastBeatIntervalMs, fadeToStartup);
    beatStrengthForWave = lerpf(beatStrengthForWave, s_startupBeatStrength, fadeToStartup);
  }
  if (beatPeriodMs < (float)g_config.beatDecayMinMs) beatPeriodMs = (float)g_config.beatDecayMinMs;
  if (beatPeriodMs > (float)g_config.beatDecayMaxMs) beatPeriodMs = (float)g_config.beatDecayMaxMs;

  // Smooth the beat period over time.
  if (s_smoothedBeatPeriodMs <= 0.0f) {
    s_smoothedBeatPeriodMs = beatPeriodMs;
  } else {
    s_smoothedBeatPeriodMs =
      (1.0f - BEAT_PERIOD_EMA_ALPHA) * s_smoothedBeatPeriodMs +
      (BEAT_PERIOD_EMA_ALPHA * beatPeriodMs);
  }

  // Base brightness envelope (relative to g_config.brightness):
  // - 100% at beat peak (when BPM is valid and recent)
  // - 70% idle if no valid BPM is detected
  float baseBrightnessRatio = 0.70f;
  float pulseRatio = 1.0f;
  const bool bpmInRange =
    (lastBeatIntervalMsF >= (float)g_config.avgBeatMinMs) &&
    (lastBeatIntervalMsF <= (float)g_config.avgBeatMaxMs);
  const bool beatRecent =
    (s_lastBeatMs > 0) &&
    ((now - s_lastBeatMs) <= (uint32_t)(g_config.avgBeatMaxMs * 2));
  if (bpmInRange && beatRecent) {
    float intervalMs = (lastBeatIntervalMsF > 0.0f) ? lastBeatIntervalMsF : s_smoothedBeatPeriodMs;
    intervalMs = clampf(intervalMs, (float)g_config.avgBeatMinMs, (float)g_config.avgBeatMaxMs);
    baseBrightnessRatio = 1.0f;
    int64_t pulseNow = (int64_t)now + (int64_t)g_config.pulseLeadMs;
    if (pulseNow < 0) pulseNow = 0;
    if (pulseNow > 0xFFFFFFFFLL) pulseNow = 0xFFFFFFFFLL;
    pulseRatio = beatPulseRatio(intervalMs, (uint32_t)pulseNow);
  }

  int frameBrightness = (int)lroundf((float)g_config.brightness * baseBrightnessRatio);
  frameBrightness = constrain(frameBrightness, 0, 255);

  const float smoothedAvgMs = clampf(s_smoothedBeatPeriodMs,
                                     (float)g_config.avgBeatMinMs,
                                     (float)g_config.avgBeatMaxMs);
  const float smoothedBpm = (smoothedAvgMs > 1.0f) ? (60000.0f / smoothedAvgMs) : 0.0f;
  setAutoSwitchBpm(smoothedBpm);

  updateAnimationSwitch();
  const auto& frames = getCurrentAnimationFrames();

#if ENABLE_WEB_TELEMETRY
  if (s_telemetry) {
    s_telemetry->avgBeatIntervalMs = smoothedAvgMs;
    s_telemetry->bpm = smoothedBpm;
    s_telemetry->animationIndex = getCurrentAnimationIndex();
    s_telemetry->animationName = getCurrentAnimationName();
    s_telemetry->baseBrightnessRatio = baseBrightnessRatio;
    s_telemetry->pulseRatio = pulseRatio;
  }
#endif

  // Tell the wave engine how many frames the current animation has.
  setWaveFrameCount((int)frames.size());

  fill_solid(s_leds, s_ledCount, CRGB::Black);

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
  if (s_telemetry) {
    s_telemetry->wavePeriodMs = 0;
    s_telemetry->nextWaveInMs = 0;
  }
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
      s_leds,
      s_ledCount
    );
  }

#if ENABLE_WEB_TELEMETRY
  if (s_telemetry) {
    s_telemetry->activeWaves = (uint32_t)getWaves().size();
  }
#endif

  bool wavesAdded = false;
#if ENABLE_BEAT_WAVES
  if (g_config.enableBeatWaves) {
    const float wavePeriodMs = clampf(s_smoothedBeatPeriodMs,
                                      (float)g_config.avgBeatMinMs,
                                      (float)g_config.avgBeatMaxMs);
    const uint32_t periodMs = (uint32_t)lroundf(wavePeriodMs);
    if (periodMs > 0) {
      if (s_nextWaveDueMs == 0) {
        s_nextWaveDueMs = now + periodMs;
        s_lastWavePeriodMs = periodMs;
      } else if (periodMs != s_lastWavePeriodMs) {
        if (s_lastWaveTime > 0) {
          s_nextWaveDueMs = s_lastWaveTime + periodMs;
        } else {
          s_nextWaveDueMs = now + periodMs;
        }
        s_lastWavePeriodMs = periodMs;
      }

#if ENABLE_WEB_TELEMETRY
      if (s_telemetry) {
        s_telemetry->wavePeriodMs = periodMs;
        s_telemetry->nextWaveInMs = (now >= s_nextWaveDueMs) ? 0 : (s_nextWaveDueMs - now);
      }
#endif

      if ((int32_t)(now - s_nextWaveDueMs) >= 0) {
        if (getWaves().size() >= g_config.maxActiveWaves) {
          dropOldestWave();
        }
        if (getWaves().size() < g_config.maxActiveWaves) {
          const uint32_t hue = (uint32_t)random(0, 65536);
          const int16_t hueStartDeg = (int16_t)random(-360, 361);
          const int16_t hueEndDeg = (int16_t)random(-360, 361);
          const float strength = clamp01(beatStrengthForWave);
          const int8_t speedCtl = speedControlFromPeriod(periodMs);
          float nose = 1.0f;
          float tail = 1.0f;
          computeWaveWidths(strength, &nose, &tail);
          nose = clampf(nose, WAVE_NOSE_MIN, WAVE_NOSE_MAX);
          const bool reverse = (random(0, 100) < 25);
          addWave(hue, speedCtl, nose, tail, reverse, hueStartDeg, hueEndDeg);
          wavesAdded = true;
        }

        s_lastWaveIntervalMs = (s_lastWaveTime > 0) ? (now - s_lastWaveTime) : 0;
        s_lastWaveTime = now;

#if ENABLE_WEB_TELEMETRY
        if (s_telemetry) {
          s_telemetry->lastWaveMs = now;
          s_telemetry->lastWaveIntervalMs = s_lastWaveIntervalMs;
          s_telemetry->activeWaves = (uint32_t)getWaves().size();
        }
#endif

#if DEBUG_WAVE_TIMING
        Serial.printf("Wave: interval=%lums period=%lums active=%u\n",
                      (unsigned long)s_lastWaveIntervalMs,
                      (unsigned long)periodMs,
                      (unsigned)getWaves().size());
#endif

        do {
          s_nextWaveDueMs += periodMs;
        } while ((int32_t)(now - s_nextWaveDueMs) >= 0);
      }
    }
  } else {
    s_nextWaveDueMs = 0;
  }
#endif

#if ENABLE_FALLBACK_WAVES
  if (!g_config.enableBeatWaves && g_config.enableFallbackWaves) {
    s_nextWaveDueMs = 0;
    // Inject a wave only if we haven't detected any beat for a while.
    // Note: if the music tempo is slower than fallbackMs (e.g. < 75 BPM),
    // this will also inject waves between beats.
    if ((now - s_lastBeatMs >= g_config.fallbackMs) && (now - s_lastWaveTime >= g_config.fallbackMs)) {
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
      s_lastWaveIntervalMs = (s_lastWaveTime > 0) ? (now - s_lastWaveTime) : 0;
      s_lastWaveTime = now;
#if ENABLE_WEB_TELEMETRY
      if (s_telemetry) {
        s_telemetry->lastWaveMs = now;
        s_telemetry->lastWaveIntervalMs = s_lastWaveIntervalMs;
        s_telemetry->activeWaves = (uint32_t)getWaves().size();
      }
#endif

      if (wavesAdded) {
        applyWaveSpacing(WAVE_SPACING_MIX, WAVE_NOSE_MIN, WAVE_NOSE_MAX);
        lastSpacingMs = now;
      }
#if DEBUG_WAVE_TIMING
      Serial.printf("Wave(fallback): interval=%lums fallback=%lums active=%u\n",
                    (unsigned long)s_lastWaveIntervalMs,
                    (unsigned long)g_config.fallbackMs,
                    (unsigned)getWaves().size());
#endif
    }
  }
#endif

  if (pulseRatio < 0.999f) {
    applyPulseToStrip(s_leds, s_ledCount, pulseRatio);
  }
}
