#include "animation_engine.h"

#include <algorithm>
#include <math.h>
#include <vector>

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
static uint32_t s_lastPulseBeatMs = 0;
static uint32_t s_lastPulseIntervalMs = 0;
static uint32_t s_startupMs = 0;
static uint32_t s_fadeStartMs = 0;
static uint32_t s_lastSyntheticBeatMs = 0;

static bool s_startupSnapshotTaken = false;
static float s_startupAvgBeatMs = 500.0f;
static float s_startupBeatStrength = 0.7f;
static float s_startupLastBeatIntervalMs = 0.0f;
static bool s_fadeSnapshotTaken = false;
static float s_fadeFromAvgBeatMs = 500.0f;
static float s_fadeFromBeatStrength = 0.7f;
static float s_fadeFromLastBeatIntervalMs = 0.0f;

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

static int32_t random_hue_drift_offset() {
  float rounds = WAVE_HUE_DRIFT_ROUNDS;
  if (rounds < 0.0f) rounds = -rounds;
  if (rounds > 32.0f) rounds = 32.0f;
  const int32_t maxOffset = (int32_t)lroundf(rounds * 65535.0f);
  if (maxOffset <= 0) return 0;
  return (int32_t)random_int(-maxOffset, maxOffset + 1);
}

static float beatPulseRatio(float beatPeriodMs, uint32_t nowMs, float minRatio, uint32_t lastBeatMs) {
  if (lastBeatMs == 0 || beatPeriodMs <= 1.0f) return kBrightnessMaxRatio;

  const uint32_t dt = nowMs - lastBeatMs;
  if (dt >= (uint32_t)beatPeriodMs) return minRatio;

  float e = 1.0f - ((float)dt / beatPeriodMs);
#if BEAT_DECAY_EASE_OUT
  e *= e;
#endif
  const float ratio = minRatio + ((kBrightnessMaxRatio - minRatio) * e);
  return clampf(ratio, minRatio, kBrightnessMaxRatio);
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

static void getWaveRatios(float* outNose, float* outGap, float* outTail) {
  const float rawNose = clamp01(g_config.waveNoseRatio);
  const float rawGap = clamp01(g_config.waveGapRatio);
  // Direct split: nose + gap + tail = 1.
  // If nose+gap exceeds 1, scale them down proportionally and leave tail at 0.
  float nose = rawNose;
  float gap = rawGap;
  const float used = nose + gap;
  if (used > 1.0f) {
    const float inv = 1.0f / used;
    nose *= inv;
    gap *= inv;
  }
  float tail = 1.0f - (nose + gap);
  if (tail < 0.0f) tail = 0.0f;
  if (outNose) *outNose = nose;
  if (outGap) *outGap = gap;
  if (outTail) *outTail = tail;
}

static int8_t speedControlFromPeriod(uint32_t periodMs) {
  const float bpm = (periodMs > 1.0f) ? (60000.0f / (float)periodMs) : 0.0f;
  const float bpmMin = (g_config.avgBeatMaxMs > 0) ? (60000.0f / (float)g_config.avgBeatMaxMs) : 0.0f;
  const float bpmMax = (g_config.avgBeatMinMs > 0) ? (60000.0f / (float)g_config.avgBeatMinMs) : 0.0f;
  if (bpmMax <= bpmMin) return 0;

  float t = (bpm - bpmMin) / (bpmMax - bpmMin);
  t = clampf(t, 0.0f, 1.0f);
  const float sc = -10.0f + (t * 20.0f);
  return (int8_t)lroundf(clampf(sc, -10.0f, 10.0f));
}

static void applyWaveRatiosToLastWave(float beatPeriodMs) {
  if (beatPeriodMs <= 0.0f) return;
  auto& waves = getWavesMutable();
  if (waves.empty()) return;
  Wave& wave = waves.back();
  const float spacingFrames = fabsf(wave.speed) * (beatPeriodMs / 1000.0f);
  float noseRatio = 0.0f;
  float tailRatio = 0.0f;
  getWaveRatios(&noseRatio, nullptr, &tailRatio);
  wave.noseWidth = spacingFrames * noseRatio;
  wave.tailWidth = spacingFrames * tailRatio;
}

static void applyWaveRatiosFromSpacing() {
  auto& waves = getWavesMutable();
  if (waves.size() < 2) return;
  float noseRatio = 0.0f;
  float tailRatio = 0.0f;
  getWaveRatios(&noseRatio, nullptr, &tailRatio);

  std::vector<int> forward;
  std::vector<int> reverse;
  forward.reserve(waves.size());
  reverse.reserve(waves.size());
  for (size_t i = 0; i < waves.size(); i++) {
    if (waves[i].reverse) reverse.push_back((int)i);
    else forward.push_back((int)i);
  }

  auto sortByCenter = [&](std::vector<int>& idx) {
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
      return waves[a].center < waves[b].center;
    });
  };

  if (forward.size() >= 2) {
    sortByCenter(forward);
    for (size_t i = 1; i < forward.size(); i++) {
      const int leader = forward[i];
      const int follower = forward[i - 1];
      float spacing = waves[leader].center - waves[follower].center;
      if (spacing < 0.0f) spacing = 0.0f;
      waves[follower].noseWidth = spacing * noseRatio;
      waves[leader].tailWidth = spacing * tailRatio;
    }
  }

  if (reverse.size() >= 2) {
    sortByCenter(reverse);
    for (size_t i = 1; i < reverse.size(); i++) {
      const int leader = reverse[i - 1];
      const int follower = reverse[i];
      float spacing = waves[follower].center - waves[leader].center;
      if (spacing < 0.0f) spacing = 0.0f;
      waves[follower].noseWidth = spacing * noseRatio;
      waves[leader].tailWidth = spacing * tailRatio;
    }
  }
}

void animationEngineInit(Rgb* leds, uint16_t ledCount) {
  s_leds = leds;
  s_ledCount = ledCount;
}

void animationEngineSetBuffer(Rgb* leds) {
  s_leds = leds;
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
  s_lastPulseBeatMs = 0;
  s_lastPulseIntervalMs = 0;
  s_startupSnapshotTaken = false;
  s_startupMs = nowMs;
  s_fadeSnapshotTaken = false;
  s_fadeStartMs = 0;
  s_lastSyntheticBeatMs = 0;
}

void runLedAnimation(uint32_t now) {
  if (!s_leds || s_ledCount == 0) return;

  if (s_startupMs == 0) s_startupMs = now;

  if (!s_startupSnapshotTaken) {
    s_startupSnapshotTaken = true;
    s_startupAvgBeatMs = getAverageBeatIntervalMs();
    s_startupBeatStrength = s_lastBeatStrength;
    s_startupLastBeatIntervalMs =
      (s_lastBeatIntervalMs > 0) ? (float)s_lastBeatIntervalMs : s_startupAvgBeatMs;
  }

#if ENABLE_BEAT_WAVES
  bool beatEvent = false;
  const uint32_t lastRealBeatBefore = getLastRealBeatMs();
  float beatStrength = 0.0f;
  const bool beatConsumed = consumeBeat(&beatStrength);
  const uint32_t lastRealBeatAfter = getLastRealBeatMs();
  const bool beatIsReal = beatConsumed && (lastRealBeatAfter != lastRealBeatBefore);
  const uint32_t refBeatMsForFakeWindow = (lastRealBeatAfter > 0) ? lastRealBeatAfter : s_startupMs;
  const uint32_t idleMs = (refBeatMsForFakeWindow > 0 && now >= refBeatMsForFakeWindow)
    ? (now - refBeatMsForFakeWindow)
    : 0;
  const bool inFakeWindow = (idleMs < FADE_TO_STARTUP_IDLE_MS);

  if (beatConsumed && (beatIsReal || inFakeWindow)) {
    beatEvent = true;
    if (s_lastBeatMs > 0) {
      s_lastBeatIntervalMs = now - s_lastBeatMs;
    }
    s_lastBeatMs = now;
    s_lastBeatStrength = beatStrength;
    if (beatIsReal) {
      s_lastSyntheticBeatMs = 0;
      if (s_lastPulseBeatMs > 0) {
        s_lastPulseIntervalMs = now - s_lastPulseBeatMs;
      }
      s_lastPulseBeatMs = now;
    }
  } else if (!beatConsumed && inFakeWindow) {
    uint32_t intervalMs = s_lastBeatIntervalMs;
    if (intervalMs == 0) {
      intervalMs = (uint32_t)lroundf(getAverageBeatIntervalMs());
    }
    if (intervalMs < g_config.avgBeatMinMs) intervalMs = g_config.avgBeatMinMs;
    if (intervalMs > g_config.avgBeatMaxMs) intervalMs = g_config.avgBeatMaxMs;
    if (intervalMs == 0) intervalMs = 500;
    if (s_lastSyntheticBeatMs == 0 || (now - s_lastSyntheticBeatMs) >= intervalMs) {
      s_lastSyntheticBeatMs = now;
      s_lastBeatMs = now;
      s_lastBeatIntervalMs = intervalMs;
      if (s_lastBeatStrength <= 0.001f) s_lastBeatStrength = 0.7f;
      beatEvent = true;
    }
  }
#endif

  float fadeToStartup = 0.0f;
#if ENABLE_FADE_TO_STARTUP
  const uint32_t lastRealBeatMs = getLastRealBeatMs();
  const uint32_t refBeatMs = (lastRealBeatMs > 0) ? lastRealBeatMs : s_startupMs;
  if (refBeatMs > 0) {
    const uint32_t idleMs = now - refBeatMs;
    if (idleMs > FADE_TO_STARTUP_IDLE_MS) {
      const uint32_t overMs = idleMs - FADE_TO_STARTUP_IDLE_MS;
      const float denom = (FADE_TO_STARTUP_DURATION_MS > 0)
        ? (float)FADE_TO_STARTUP_DURATION_MS
        : 1.0f;
      fadeToStartup = clamp01((float)overMs / denom);
    }
  }
#endif

  if (fadeToStartup > 0.0f) {
    if (s_fadeStartMs == 0) s_fadeStartMs = now;
  } else {
    s_fadeStartMs = 0;
  }

  setWaveSpeedBase(g_config.waveSpeedBase);
  setWaveSpeedRange(g_config.waveSpeedRange);
  setWaveSpeedMultiplier(g_config.waveSpeedMultiplier);

  float beatPeriodMs = getAverageBeatIntervalMs();
  const float lastBeatIntervalMsRaw = (float)s_lastBeatIntervalMs;
  float lastBeatIntervalMsF = lastBeatIntervalMsRaw;
  float beatStrengthForWave = s_lastBeatStrength;
  if (fadeToStartup > 0.0f) {
    if (!s_fadeSnapshotTaken) {
      s_fadeSnapshotTaken = true;
      s_fadeFromAvgBeatMs = beatPeriodMs;
      s_fadeFromLastBeatIntervalMs =
        (lastBeatIntervalMsRaw > 0.0f) ? lastBeatIntervalMsRaw : beatPeriodMs;
      s_fadeFromBeatStrength = beatStrengthForWave;
    }
    beatPeriodMs = lerpf(s_fadeFromAvgBeatMs, s_startupAvgBeatMs, fadeToStartup);
    const float startupIntervalTargetMs =
      (s_startupLastBeatIntervalMs > 0.0f) ? s_startupLastBeatIntervalMs : s_startupAvgBeatMs;
    lastBeatIntervalMsF = lerpf(s_fadeFromLastBeatIntervalMs, startupIntervalTargetMs, fadeToStartup);
    beatStrengthForWave = lerpf(s_fadeFromBeatStrength, s_startupBeatStrength, fadeToStartup);
  } else {
    s_fadeSnapshotTaken = false;
  }
  float effectiveIntervalMs = (lastBeatIntervalMsF > 0.0f) ? lastBeatIntervalMsF : beatPeriodMs;
  if (effectiveIntervalMs < (float)g_config.beatDecayMinMs) effectiveIntervalMs = (float)g_config.beatDecayMinMs;
  if (effectiveIntervalMs > (float)g_config.beatDecayMaxMs) effectiveIntervalMs = (float)g_config.beatDecayMaxMs;
  beatPeriodMs = effectiveIntervalMs;

#if ENABLE_BEAT_WAVES
  if (g_config.enableBeatWaves && fadeToStartup > 0.0f && !beatEvent) {
    uint32_t periodMs = (uint32_t)lroundf(beatPeriodMs);
    if (periodMs < 1) periodMs = 1;
    if (s_nextWaveDueMs == 0) {
      s_nextWaveDueMs = (s_lastWaveTime > 0) ? (s_lastWaveTime + periodMs) : (now + periodMs);
      s_lastWavePeriodMs = periodMs;
    } else if (periodMs != s_lastWavePeriodMs) {
      s_nextWaveDueMs = (s_lastWaveTime > 0) ? (s_lastWaveTime + periodMs) : (now + periodMs);
      s_lastWavePeriodMs = periodMs;
    }

    if ((int32_t)(now - s_nextWaveDueMs) >= 0) {
      beatEvent = true;
      s_lastBeatMs = now;
      s_lastBeatIntervalMs = periodMs;
      s_lastSyntheticBeatMs = now;
      if (s_lastBeatStrength <= 0.001f) s_lastBeatStrength = 0.7f;
      do {
        s_nextWaveDueMs += periodMs;
      } while ((int32_t)(now - s_nextWaveDueMs) >= 0);
    }
  } else if (fadeToStartup <= 0.0f) {
    s_nextWaveDueMs = 0;
    s_lastWavePeriodMs = 0;
  }
#endif

  if (s_smoothedBeatPeriodMs <= 0.0f) {
    s_smoothedBeatPeriodMs = beatPeriodMs;
  } else {
    s_smoothedBeatPeriodMs =
      (1.0f - BEAT_PERIOD_EMA_ALPHA) * s_smoothedBeatPeriodMs +
      (BEAT_PERIOD_EMA_ALPHA * beatPeriodMs);
  }

  bool spawnWaveNow = false;
#if ENABLE_BEAT_WAVES
  if (g_config.enableBeatWaves) {
    // Spawn exactly once per beat event (real, synthetic, or relaxation tick).
    spawnWaveNow = beatEvent;
  }
#endif

  float baseBrightnessRatio = 0.70f;
  float pulseRatio = 1.0f;
  if (s_lastPulseBeatMs > 0) {
    float pulseIntervalMs = (s_lastPulseIntervalMs > 0.0f) ? (float)s_lastPulseIntervalMs : s_smoothedBeatPeriodMs;
    float intervalMs = pulseIntervalMs;
    intervalMs *= clampf(g_config.beatPulseDecayRatio, 0.1f, 5.0f);
    if (intervalMs < 1.0f) intervalMs = 1.0f;
    baseBrightnessRatio = 1.0f;
    int64_t pulseNow = (int64_t)now + (int64_t)g_config.pulseLeadMs;
    if (pulseNow < 0) pulseNow = 0;
    if (pulseNow > 0xFFFFFFFFLL) pulseNow = 0xFFFFFFFFLL;
    const float minRatio = clampf(g_config.beatPulseMinRatio, 0.0f, 1.0f);
    pulseRatio = beatPulseRatio(intervalMs, (uint32_t)pulseNow, minRatio, s_lastPulseBeatMs);
  }

  if (fadeToStartup > 0.0f) {
    baseBrightnessRatio = lerpf(baseBrightnessRatio, 0.70f, fadeToStartup);
    pulseRatio = lerpf(pulseRatio, 1.0f, fadeToStartup);
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
    applyWaveRatiosFromSpacing();
    lastSpacingMs = now;
  }

  const float maxFrameIndex = (frames.size() > 0) ? (float)(frames.size() - 1) : 0.0f;
  auto& waves = getWavesMutable();
  for (size_t i = 0; i < waves.size(); ) {
    const auto& wave = waves[i];
    const int lit = renderInterpolatedFrame(
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
    if (lit <= 0 && wave.center >= 0.0f && wave.center <= maxFrameIndex) {
      waves.erase(waves.begin() + (int)i);
      continue;
    }
    i++;
  }

#if ENABLE_BEAT_WAVES
  if (g_config.enableBeatWaves && spawnWaveNow) {
    if (getWaves().size() >= g_config.maxActiveWaves) {
      dropOldestWave();
    }
    if (getWaves().size() < g_config.maxActiveWaves) {
      const uint32_t hue = (uint32_t)random_int(0, 65536);
      const int32_t hueStartOffset = 0;
      const int32_t hueEndOffset = random_hue_drift_offset();
      const int8_t speedCtl = speedControlFromPeriod((uint32_t)lroundf(beatPeriodMs));
      const bool reverse = false;
      addWave(hue, speedCtl, 1.0f, 1.0f, reverse, hueStartOffset, hueEndOffset);
      applyWaveRatiosToLastWave(beatPeriodMs);
    }

    s_lastWaveIntervalMs = (s_lastWaveTime > 0) ? (now - s_lastWaveTime) : 0;
    s_lastWaveTime = now;
  }
#endif

#if ENABLE_FALLBACK_WAVES
  if (g_config.enableFallbackWaves) {
    if (!g_config.enableBeatWaves) {
      s_nextWaveDueMs = 0;
    }
    const uint32_t lastRealBeatMs = getLastRealBeatMs();
    const uint32_t refBeatMs = (lastRealBeatMs > 0) ? lastRealBeatMs : s_lastBeatMs;
    const bool beatQuiet = (refBeatMs == 0) ? true : (now - refBeatMs >= g_config.fallbackMs);
    const bool waveQuiet = (now - s_lastWaveTime >= g_config.fallbackMs);
    if (beatQuiet && waveQuiet) {
      if (getWaves().size() < g_config.maxActiveWaves) {
        const uint32_t hue = (uint32_t)random_int(0, 65536);
        const int8_t speedCtl = speedControlFromPeriod(g_config.fallbackMs);
        const int32_t hueStartOffset = 0;
        const int32_t hueEndOffset = random_hue_drift_offset();
        addWave(hue, speedCtl, 1.0f, 1.0f, false, hueStartOffset, hueEndOffset);
        applyWaveRatiosToLastWave((float)g_config.fallbackMs);
      }
      s_lastWaveIntervalMs = (s_lastWaveTime > 0) ? (now - s_lastWaveTime) : 0;
      s_lastWaveTime = now;

      applyWaveRatiosFromSpacing();
      lastSpacingMs = now;
    }
  }
#endif

  if (pulseRatio < 0.999f) {
    applyPulseToStrip(s_leds, s_ledCount, pulseRatio);
  }
}
