#include "animation_engine.h"

#include <math.h>

#include "animation_manager.h"
#include "audio_processor.h"
#include "elsa_config.h"
#include "frame_interpolation.h"
#include "runtime_config.h"
#include "wave_position.h"

#include "esp_random.h"

static Rgb* s_leds = nullptr;
static uint16_t s_ledCount = 0;

static uint32_t s_lastWaveTime = 0;
static uint32_t s_lastWaveIntervalMs = 0;
static uint32_t s_lastWavePeriodMs = 0;
static uint32_t s_nextWaveDueMs = 0;
static float s_smoothedBeatPeriodMs = 0.0f;

static uint32_t s_lastBeatMs = 0;
static float s_lastBeatStrength = 0.7f;
static uint32_t s_lastBeatIntervalMs = 0;

static bool s_startupSnapshotTaken = false;
static float s_startupAvgBeatMs = 500.0f;
static float s_startupBeatStrength = 0.7f;
static float s_startupLastBeatIntervalMs = 0.0f;

static constexpr float kBrightnessMinRatio = 0.30f;
static constexpr float kBrightnessMaxRatio = 1.00f;

static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static inline float lerpf(float a, float b, float t) {
  return a + (b - a) * t;
}

static inline int random_int(int min_val, int max_val) {
  if (max_val <= min_val) return min_val;
  const uint32_t span = (uint32_t)(max_val - min_val);
  return min_val + (int)(esp_random() % span);
}

static float beatPulseRatio(float beatPeriodMs, uint32_t nowMs) {
  if (s_lastBeatMs == 0 || beatPeriodMs <= 1.0f) return kBrightnessMaxRatio;

  const uint32_t dt = nowMs - s_lastBeatMs;
  if (dt >= (uint32_t)beatPeriodMs) return kBrightnessMinRatio;

  float e = 1.0f - ((float)dt / beatPeriodMs);
#if BEAT_DECAY_EASE_OUT
  e *= e;
#endif
  const float ratio = kBrightnessMinRatio + ((kBrightnessMaxRatio - kBrightnessMinRatio) * e);
  return clampf(ratio, kBrightnessMinRatio, kBrightnessMaxRatio);
}

static void applyPulseToStrip(Rgb* leds, int count, float ratio) {
  if (!leds || count == 0) return;
  if (ratio >= 0.999f) return;
  if (ratio <= 0.0f) {
    fill_solid(leds, count, {0, 0, 0});
    return;
  }
  uint16_t scale = (uint16_t)lroundf(ratio * 255.0f);
  if (scale > 255) scale = 255;
  scale_rgb(leds, count, (uint8_t)scale);
}

static void computeWaveWidths(float strength, float* outNose, float* outTail) {
  const float t = clampf(strength, 0.0f, 1.0f);
  const float attack = lerpf(WAVE_ATTACK_MIN, WAVE_ATTACK_MAX, t);
  const float sustain = lerpf(WAVE_SUSTAIN_MIN, WAVE_SUSTAIN_MAX, t);
  const float release = lerpf(WAVE_RELEASE_MIN, WAVE_RELEASE_MAX, t);
  const float decay = lerpf(WAVE_DECAY_MIN, WAVE_DECAY_MAX, t);

  const float nose = attack + decay;
  const float tail = sustain + release;

  if (outNose) *outNose = nose * WAVE_WIDTH_SCALE;
  if (outTail) *outTail = tail * WAVE_WIDTH_SCALE;
}

static int8_t speedControlFromPeriod(uint32_t periodMs) {
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
  return (int8_t)clampf((float)sc, -10.0f, 10.0f);
}

void animationEngineInit(Rgb* leds, uint16_t ledCount) {
  s_leds = leds;
  s_ledCount = ledCount;
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

void runLedAnimation(uint32_t now) {
  if (!s_leds || s_ledCount == 0) return;

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

  if (s_smoothedBeatPeriodMs <= 0.0f) {
    s_smoothedBeatPeriodMs = beatPeriodMs;
  } else {
    s_smoothedBeatPeriodMs =
      (1.0f - BEAT_PERIOD_EMA_ALPHA) * s_smoothedBeatPeriodMs +
      (BEAT_PERIOD_EMA_ALPHA * beatPeriodMs);
  }

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
  if (frameBrightness < 0) frameBrightness = 0;
  if (frameBrightness > 255) frameBrightness = 255;

  const float smoothedAvgMs = clampf(s_smoothedBeatPeriodMs,
                                     (float)g_config.avgBeatMinMs,
                                     (float)g_config.avgBeatMaxMs);
  const float smoothedBpm = (smoothedAvgMs > 1.0f) ? (60000.0f / smoothedAvgMs) : 0.0f;
  setAutoSwitchBpm(smoothedBpm);

  updateAnimationSwitch();
  const auto& frames = getCurrentAnimationFrames();

  setWaveFrameCount((int)frames.size());
  fill_solid(s_leds, s_ledCount, {0, 0, 0});

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

  for (const auto& wave : getWaves()) {
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

      if ((int32_t)(now - s_nextWaveDueMs) >= 0) {
        if (getWaves().size() >= g_config.maxActiveWaves) {
          dropOldestWave();
        }
        if (getWaves().size() < g_config.maxActiveWaves) {
          const uint32_t hue = (uint32_t)random_int(0, 65536);
          const int16_t hueStartDeg = (int16_t)random_int(-360, 361);
          const int16_t hueEndDeg = (int16_t)random_int(-360, 361);
          const float strength = clamp01(beatStrengthForWave);
          const int8_t speedCtl = speedControlFromPeriod(periodMs);
          float nose = 1.0f;
          float tail = 1.0f;
          computeWaveWidths(strength, &nose, &tail);
          nose = clampf(nose, WAVE_NOSE_MIN, WAVE_NOSE_MAX);
          const bool reverse = (random_int(0, 100) < 25);
          addWave(hue, speedCtl, nose, tail, reverse, hueStartDeg, hueEndDeg);
        }

        s_lastWaveIntervalMs = (s_lastWaveTime > 0) ? (now - s_lastWaveTime) : 0;
        s_lastWaveTime = now;

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
    if ((now - s_lastBeatMs >= g_config.fallbackMs) && (now - s_lastWaveTime >= g_config.fallbackMs)) {
      if (getWaves().size() < g_config.maxActiveWaves) {
        const uint32_t hue = (uint32_t)random_int(0, 65536);
        const int16_t hueStartDeg = (int16_t)random_int(-360, 361);
        const int16_t hueEndDeg = (int16_t)random_int(-360, 361);
        const int8_t speedCtl = speedControlFromPeriod(g_config.fallbackMs);
        float nose = 1.0f;
        float tail = 1.0f;
        computeWaveWidths(0.0f, &nose, &tail);
        nose = clampf(nose, WAVE_NOSE_MIN, WAVE_NOSE_MAX);
        addWave(hue, speedCtl, nose, tail, false, hueStartDeg, hueEndDeg);
      }
      s_lastWaveIntervalMs = (s_lastWaveTime > 0) ? (now - s_lastWaveTime) : 0;
      s_lastWaveTime = now;

      applyWaveSpacing(WAVE_SPACING_MIX, WAVE_NOSE_MIN, WAVE_NOSE_MAX);
      lastSpacingMs = now;
    }
  }
#endif

  if (pulseRatio < 0.999f) {
    applyPulseToStrip(s_leds, s_ledCount, pulseRatio);
  }
}
