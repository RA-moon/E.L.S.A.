#include "animation_engine.h"

#include <algorithm>
#include <math.h>
#include <vector>

#include "animation_manager.h"
#include "animation_config_table.h"
#include "audio_processor.h"
#include "beat_scheduler_policy.h"
#include "elsa_config.h"
#include "frame_interpolation.h"
#include "runtime_config.h"
#include "wave_position.h"

#include "esp_random.h"

static Rgb* s_leds = nullptr;
static uint16_t s_ledCount = 0;

static uint32_t s_lastWaveTime = 0;
static uint32_t s_lastWavePeriodMs = 0;
static uint32_t s_nextWaveDueMs = 0;
static float s_smoothedBeatPeriodMs = 0.0f;

static uint32_t s_lastBeatMs = 0;
static uint32_t s_lastBeatIntervalMs = 0;
static uint32_t s_lastPulseBeatMs = 0;
static uint32_t s_lastPulseIntervalMs = 0;
static uint32_t s_startupMs = 0;
static uint32_t s_lastSyntheticBeatMs = 0;
static float s_beatTriggerAccumulator = 0.0f;
static float s_fallbackTriggerAccumulator = 0.0f;

static bool s_startupSnapshotTaken = false;
static float s_startupAvgBeatMs = 500.0f;
static float s_startupLastBeatIntervalMs = 0.0f;
static bool s_fadeSnapshotTaken = false;
static float s_fadeFromAvgBeatMs = 500.0f;
static float s_fadeFromLastBeatIntervalMs = 0.0f;
static AnimationSchedulerTelemetry s_schedulerTelemetry = {
  0,
  0,
  WaveSpawnReason::None,
};

static constexpr float kBrightnessMaxRatio = 1.00f;
static constexpr float kFallbackBeatOverdueRatio = 1.20f;

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

static int32_t random_hue_drift_offset(float rounds) {
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

// Converts a trigger scale [0..1] to deterministic fractional triggering.
// 1.0 triggers every event, 0.5 every second event on average, 0.0 never.
static bool consumeScaledTrigger(bool eventNow, float scale, float* accumulator) {
  if (!eventNow || !accumulator) return false;
  scale = clamp01(scale);
  if (scale <= 0.0f) return false;
  if (scale >= 1.0f) return true;
  *accumulator += scale;
  if (*accumulator >= 1.0f) {
    *accumulator -= 1.0f;
    return true;
  }
  return false;
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

static void getWaveRatios(float noseRatio, float tailRatio, float* outNose, float* outGap, float* outTail) {
  float nose = clamp01(noseRatio);
  float tail = clamp01(tailRatio);
  const float used = nose + tail;
  if (used > 1.0f) {
    const float inv = 1.0f / used;
    nose *= inv;
    tail *= inv;
  }
  float gap = 1.0f - (nose + tail);
  if (gap < 0.0f) gap = 0.0f;
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

static void applyWaveRatiosToLastWave(float beatPeriodMs, float noseRatio, float tailRatio) {
  if (beatPeriodMs <= 0.0f) return;
  auto& waves = getWavesMutable();
  if (waves.empty()) return;
  Wave& wave = waves.back();
  const float spacingFrames = fabsf(wave.speed) * (beatPeriodMs / 1000.0f);
  float noseRatioResolved = 0.0f;
  float tailRatioResolved = 0.0f;
  getWaveRatios(noseRatio, tailRatio, &noseRatioResolved, nullptr, &tailRatioResolved);
  wave.noseWidth = spacingFrames * noseRatioResolved;
  wave.tailWidth = spacingFrames * tailRatioResolved;
}

static void applyWaveRatiosFromSpacing(float noseRatio, float tailRatio) {
  auto& waves = getWavesMutable();
  if (waves.size() < 2) return;
  float noseRatioResolved = 0.0f;
  float tailRatioResolved = 0.0f;
  getWaveRatios(noseRatio, tailRatio, &noseRatioResolved, nullptr, &tailRatioResolved);

  std::vector<int> ordered;
  ordered.reserve(waves.size());
  for (size_t i = 0; i < waves.size(); i++) {
    ordered.push_back((int)i);
  }

  auto sortByCenter = [&](std::vector<int>& idx) {
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
      return waves[a].center < waves[b].center;
    });
  };

  if (ordered.size() >= 2) {
    sortByCenter(ordered);
    for (size_t i = 1; i < ordered.size(); i++) {
      const int leader = ordered[i];
      const int follower = ordered[i - 1];
      float spacing = waves[leader].center - waves[follower].center;
      if (spacing < 0.0f) spacing = 0.0f;
      waves[follower].noseWidth = spacing * noseRatioResolved;
      waves[leader].tailWidth = spacing * tailRatioResolved;
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
  s_lastWavePeriodMs = 0;
  s_nextWaveDueMs = 0;
  s_smoothedBeatPeriodMs = 0.0f;
  s_lastBeatMs = 0;
  s_lastBeatIntervalMs = 0;
  s_lastPulseBeatMs = 0;
  s_lastPulseIntervalMs = 0;
  s_startupSnapshotTaken = false;
  s_startupMs = nowMs;
  s_fadeSnapshotTaken = false;
  s_lastSyntheticBeatMs = 0;
  s_beatTriggerAccumulator = 0.0f;
  s_fallbackTriggerAccumulator = 0.0f;
  s_schedulerTelemetry.lastSpawnMs = 0;
  s_schedulerTelemetry.lastFallbackIntervalMs = 0;
  s_schedulerTelemetry.lastSpawnReason = WaveSpawnReason::None;
}

void getAnimationSchedulerTelemetry(AnimationSchedulerTelemetry* out) {
  if (!out) return;
  *out = s_schedulerTelemetry;
}

void runLedAnimation(uint32_t now) {
  if (!s_leds || s_ledCount == 0) return;

  if (s_startupMs == 0) s_startupMs = now;

  if (!s_startupSnapshotTaken) {
    s_startupSnapshotTaken = true;
    s_startupAvgBeatMs = getAverageBeatIntervalMs();
    s_startupLastBeatIntervalMs =
      (s_lastBeatIntervalMs > 0) ? (float)s_lastBeatIntervalMs : s_startupAvgBeatMs;
  }

  const uint32_t lastRealBeatForPhaseMs = getLastRealBeatMs();
  const uint32_t refBeatMsForPhase = (lastRealBeatForPhaseMs > 0) ? lastRealBeatForPhaseMs : s_startupMs;
  const uint32_t idleMsForPhase = (refBeatMsForPhase > 0 && now >= refBeatMsForPhase)
    ? (now - refBeatMsForPhase)
    : 0;
  const bool inFakeWindowForPhase = (idleMsForPhase < FADE_TO_STARTUP_IDLE_MS);
  const float waitProgress = (FADE_TO_STARTUP_IDLE_MS > 0)
    ? clamp01((float)idleMsForPhase / (float)FADE_TO_STARTUP_IDLE_MS)
    : 1.0f;

  bool beatEvent = false;
  WaveSpawnReason beatSpawnReason = WaveSpawnReason::None;
#if ENABLE_BEAT_WAVES
  BeatEvent beat = {0.0f, 0, false};
  const bool beatConsumed = consumeBeat(&beat);
  const bool beatIsReal = beatConsumed && beat.isReal;
  bool syntheticFakeDue = false;
  uint32_t syntheticIntervalMs = 0;

  if (!beatConsumed && inFakeWindowForPhase) {
    syntheticIntervalMs = s_lastBeatIntervalMs;
    if (syntheticIntervalMs == 0) {
      syntheticIntervalMs = (uint32_t)lroundf(getAverageBeatIntervalMs());
    }
    if (syntheticIntervalMs < g_config.avgBeatMinMs) syntheticIntervalMs = g_config.avgBeatMinMs;
    if (syntheticIntervalMs > g_config.avgBeatMaxMs) syntheticIntervalMs = g_config.avgBeatMaxMs;
    if (syntheticIntervalMs == 0) syntheticIntervalMs = 500;
    syntheticFakeDue = (s_lastSyntheticBeatMs == 0 || (now - s_lastSyntheticBeatMs) >= syntheticIntervalMs);
  }

  const BeatFlowResult beatFlow = evaluateBeatFlow({
    beatConsumed,
    beatIsReal,
    inFakeWindowForPhase,
    syntheticFakeDue,
    false
  });

  if (beatFlow.emitBeatEvent) {
    beatEvent = true;
    beatSpawnReason = beatFlow.reason;
    if (beatConsumed) {
      uint32_t beatTimeMs = beat.timestampMs;
      if (beatTimeMs == 0 || beatTimeMs > now) beatTimeMs = now;
      if (s_lastBeatMs > 0 && beatTimeMs >= s_lastBeatMs) {
        s_lastBeatIntervalMs = beatTimeMs - s_lastBeatMs;
      }
      s_lastBeatMs = beatTimeMs;
      if (beatIsReal) {
        s_lastSyntheticBeatMs = 0;
      }
    } else if (beatFlow.reason == WaveSpawnReason::FakeBeat && syntheticFakeDue) {
      s_lastSyntheticBeatMs = now;
      s_lastBeatMs = now;
      s_lastBeatIntervalMs = syntheticIntervalMs;
    }
  }
#endif

  if (beatEvent) {
    if (s_lastPulseBeatMs > 0) {
      s_lastPulseIntervalMs = now - s_lastPulseBeatMs;
    }
    s_lastPulseBeatMs = now;
  }

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

  const AnimationEffectContext effectCtx = {
    beatEvent,
    (!beatEvent && inFakeWindowForPhase && fadeToStartup <= 0.0f),
    (fadeToStartup > 0.0f),
    waitProgress,
    fadeToStartup,
  };
  ResolvedAnimationEffects effects = {};
  resolveAnimationEffects(effectCtx, g_config, &effects);
  const bool beatWavesEnabled = effects.enableBeatWaves;
  const bool fallbackWavesEnabled = effects.enableFallbackWaves;

  setWaveSpeedBase(effects.waveSpeedBase);
  setWaveSpeedRange(effects.waveSpeedRange);
  setWaveSpeedMultiplier(g_config.waveSpeedMultiplier);

  float beatPeriodMs = getAverageBeatIntervalMs();
  const float lastBeatIntervalMsRaw = (float)s_lastBeatIntervalMs;
  float lastBeatIntervalMsF = lastBeatIntervalMsRaw;
  if (fadeToStartup > 0.0f) {
    if (!s_fadeSnapshotTaken) {
      s_fadeSnapshotTaken = true;
      s_fadeFromAvgBeatMs = beatPeriodMs;
      s_fadeFromLastBeatIntervalMs =
        (lastBeatIntervalMsRaw > 0.0f) ? lastBeatIntervalMsRaw : beatPeriodMs;
    }
    beatPeriodMs = lerpf(s_fadeFromAvgBeatMs, s_startupAvgBeatMs, fadeToStartup);
    const float startupIntervalTargetMs =
      (s_startupLastBeatIntervalMs > 0.0f) ? s_startupLastBeatIntervalMs : s_startupAvgBeatMs;
    lastBeatIntervalMsF = lerpf(s_fadeFromLastBeatIntervalMs, startupIntervalTargetMs, fadeToStartup);
  } else {
    s_fadeSnapshotTaken = false;
  }
  float effectiveIntervalMs = (lastBeatIntervalMsF > 0.0f) ? lastBeatIntervalMsF : beatPeriodMs;
  if (effectiveIntervalMs < (float)g_config.beatDecayMinMs) effectiveIntervalMs = (float)g_config.beatDecayMinMs;
  if (effectiveIntervalMs > (float)g_config.beatDecayMaxMs) effectiveIntervalMs = (float)g_config.beatDecayMaxMs;
  beatPeriodMs = effectiveIntervalMs;

#if ENABLE_BEAT_WAVES
  if (beatWavesEnabled && fadeToStartup > 0.0f && !beatEvent) {
    const RelaxTickResult relax = evaluateRelaxTick(
      {
        now,
        s_lastWaveTime,
        beatPeriodMs,
      },
      {
        s_nextWaveDueMs,
        s_lastWavePeriodMs,
      }
    );
    s_nextWaveDueMs = relax.state.nextDueMs;
    s_lastWavePeriodMs = relax.state.periodMs;

    const BeatFlowResult relaxFlow = evaluateBeatFlow({
      false,
      false,
      false,
      false,
      relax.fired
    });
    if (relaxFlow.emitBeatEvent) {
      beatEvent = true;
      beatSpawnReason = relaxFlow.reason;
      s_lastBeatMs = now;
      s_lastBeatIntervalMs = s_lastWavePeriodMs;
      s_lastSyntheticBeatMs = now;
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
  if (beatWavesEnabled) {
    if (effects.beatWaveTriggerScale >= 0.999f) {
      s_beatTriggerAccumulator = 0.0f;
      spawnWaveNow = beatEvent;
    } else {
      spawnWaveNow = consumeScaledTrigger(beatEvent, effects.beatWaveTriggerScale, &s_beatTriggerAccumulator);
    }
  }
#endif

  float baseBrightnessRatio = 0.70f;
  float pulseRatio = 1.0f;
  float noseWidthPulseRatio = 1.0f;
  float tailWidthPulseRatio = 1.0f;
  if (s_lastPulseBeatMs > 0) {
    float pulseIntervalMs = (s_lastPulseIntervalMs > 0.0f) ? (float)s_lastPulseIntervalMs : s_smoothedBeatPeriodMs;
    float intervalMs = pulseIntervalMs;
    intervalMs *= clampf(g_config.beatPulseDecayRatio, 0.1f, 5.0f);
    if (intervalMs < 1.0f) intervalMs = 1.0f;
    baseBrightnessRatio = 1.0f;
    int64_t pulseNow = (int64_t)now + (int64_t)g_config.pulseLeadMs;
    if (pulseNow < 0) pulseNow = 0;
    if (pulseNow > 0xFFFFFFFFLL) pulseNow = 0xFFFFFFFFLL;
    const float minRatio = clampf(effects.brightnessPulseMinRatio, 0.0f, 1.0f);
    const float noseWidthMinRatio = clampf(effects.noseWidthScaleMin, 0.0f, 1.0f);
    const float tailWidthMinRatio = clampf(effects.tailWidthScaleMin, 0.0f, 1.0f);
    if (beatEvent) {
      pulseRatio = kBrightnessMaxRatio;
      noseWidthPulseRatio = 1.0f;
      tailWidthPulseRatio = 1.0f;
    } else {
      pulseRatio = beatPulseRatio(intervalMs, (uint32_t)pulseNow, minRatio, s_lastPulseBeatMs);
      noseWidthPulseRatio = beatPulseRatio(intervalMs, (uint32_t)pulseNow, noseWidthMinRatio, s_lastPulseBeatMs);
      tailWidthPulseRatio = beatPulseRatio(intervalMs, (uint32_t)pulseNow, tailWidthMinRatio, s_lastPulseBeatMs);
    }
  }

  if (fadeToStartup > 0.0f) {
    baseBrightnessRatio = lerpf(baseBrightnessRatio, 0.70f, fadeToStartup);
    pulseRatio = lerpf(pulseRatio, 1.0f, fadeToStartup);
    noseWidthPulseRatio = lerpf(noseWidthPulseRatio, 1.0f, fadeToStartup);
    tailWidthPulseRatio = lerpf(tailWidthPulseRatio, 1.0f, fadeToStartup);
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
    applyWaveRatiosFromSpacing(effects.waveNoseRatio, effects.waveTailRatio);
    lastSpacingMs = now;
  }

  const float maxFrameIndex = (frames.size() > 0) ? (float)(frames.size() - 1) : 0.0f;
  auto& waves = getWavesMutable();
  for (size_t i = 0; i < waves.size(); ) {
    const auto& wave = waves[i];
    const float tailWidth = wave.tailWidth * tailWidthPulseRatio;
    const float noseWidth = wave.noseWidth * noseWidthPulseRatio;
    const int lit = renderInterpolatedFrame(
      frames,
      wave.center,
      wave.hue,
      tailWidth,
      noseWidth,
      frameBrightness,
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
  if (beatWavesEnabled && spawnWaveNow) {
    bool spawned = false;
    if (getWaves().size() >= g_config.maxActiveWaves) {
      dropOldestWave();
    }
    if (getWaves().size() < g_config.maxActiveWaves) {
      const uint32_t hue = (uint32_t)random_int(0, 65536);
      const int32_t hueStartOffset = 0;
      const int32_t hueEndOffset = random_hue_drift_offset(effects.waveHueDriftRounds);
      const int8_t speedCtl = speedControlFromPeriod((uint32_t)lroundf(beatPeriodMs));
      addWave(hue, speedCtl, 1.0f, 1.0f, hueStartOffset, hueEndOffset);
      applyWaveRatiosToLastWave(beatPeriodMs, effects.waveNoseRatio, effects.waveTailRatio);
      spawned = true;
    }

    s_lastWaveTime = now;
    if (spawned) {
      s_schedulerTelemetry.lastSpawnMs = now;
      s_schedulerTelemetry.lastSpawnReason = beatSpawnReason;
    }
  }
#endif

#if ENABLE_FALLBACK_WAVES
  if (fallbackWavesEnabled) {
    if (!beatWavesEnabled) {
      s_nextWaveDueMs = 0;
    }
    const FallbackPolicyInput fallbackIn = {
      now,
      g_config.fallbackMs,
      beatWavesEnabled,
      getAverageBeatIntervalMs(),
      g_config.avgBeatMinMs,
      g_config.avgBeatMaxMs,
      getLastRealBeatMs(),
      s_startupMs,
      s_lastBeatMs,
      s_lastWaveTime,
      kFallbackBeatOverdueRatio,
    };
    const FallbackPolicyResult fallback = evaluateFallbackPolicy(fallbackIn);
    s_schedulerTelemetry.lastFallbackIntervalMs = fallback.intervalMs;

    bool spawnFallbackNow = false;
    if (effects.fallbackWaveTriggerScale >= 0.999f) {
      s_fallbackTriggerAccumulator = 0.0f;
      spawnFallbackNow = fallback.shouldSpawn;
    } else {
      spawnFallbackNow = consumeScaledTrigger(
        fallback.shouldSpawn,
        effects.fallbackWaveTriggerScale,
        &s_fallbackTriggerAccumulator
      );
    }

    if (spawnFallbackNow) {
      bool spawned = false;
      if (getWaves().size() >= g_config.maxActiveWaves) {
        dropOldestWave();
      }
      if (getWaves().size() < g_config.maxActiveWaves) {
        const uint32_t hue = (uint32_t)random_int(0, 65536);
        const int8_t speedCtl = speedControlFromPeriod(fallback.intervalMs);
        const int32_t hueStartOffset = 0;
        const int32_t hueEndOffset = random_hue_drift_offset(effects.waveHueDriftRounds);
        addWave(hue, speedCtl, 1.0f, 1.0f, hueStartOffset, hueEndOffset);
        applyWaveRatiosToLastWave((float)fallback.intervalMs, effects.waveNoseRatio, effects.waveTailRatio);
        spawned = true;
      }
      if (spawned) {
        s_lastWaveTime = now;
        s_schedulerTelemetry.lastSpawnMs = now;
        s_schedulerTelemetry.lastSpawnReason = WaveSpawnReason::Fallback;
        applyWaveRatiosFromSpacing(effects.waveNoseRatio, effects.waveTailRatio);
        lastSpacingMs = now;
      }
    }
  }
#endif

  if (pulseRatio < 0.999f) {
    applyPulseToStrip(s_leds, s_ledCount, pulseRatio);
  }
}
