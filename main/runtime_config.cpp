#include "runtime_config.h"

#include "animation_manager.h"
#include "audio_processor.h"
#include "elsa_config.h"

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

static inline float clampF(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

RuntimeConfig g_config = {
  BRIGHTNESS1,
  BEAT_PULSE_MIN_RATIO,
  BEAT_PULSE_DECAY_RATIO,
  BEAT_DECAY_MIN_MS,
  BEAT_DECAY_MAX_MS,
  0,
  NO_BEAT_FALLBACK_MS,
  MAX_ACTIVE_WAVES,
  WAVE_SPEED_BASE,
  WAVE_SPEED_RANGE,
  WAVE_SPEED_MULTIPLIER,
  WAVE_NOSE_RATIO,
  WAVE_GAP_RATIO,
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

void normalizeConfig() {
  g_config.brightness = clampU8((int)g_config.brightness, 0, 255);
  g_config.beatPulseMinRatio = clampF(g_config.beatPulseMinRatio, 0.0f, 1.0f);
  g_config.beatPulseDecayRatio = clampF(g_config.beatPulseDecayRatio, 0.1f, 5.0f);
  g_config.waveSpeedBase = clampF(g_config.waveSpeedBase, 0.0f, 10.0f);
  g_config.waveSpeedRange = clampF(g_config.waveSpeedRange, 0.0f, 1.0f);
  g_config.waveSpeedMultiplier = clampF(g_config.waveSpeedMultiplier, 0.1f, 5.0f);
  g_config.waveNoseRatio = clampF(g_config.waveNoseRatio, 0.0f, 1.0f);
  g_config.waveGapRatio = clampF(g_config.waveGapRatio, 0.0f, 1.0f);
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

void applyAnimationConfig() {
  setAnimationAutoMode(g_config.animationAuto);
  setAnimationIndex(g_config.animationIndex);
}

void applyBeatConfig() {
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
