#include "animation_config_table.h"

#include "elsa_config.h"

static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline float clamp01(float v) {
  return clampf(v, 0.0f, 1.0f);
}

static inline float lerpf(float a, float b, float t) {
  return a + (b - a) * t;
}

static constexpr OptionalFloatValue kUnsetFloat = {false, 0.0f};

static constexpr AnimationEffectOverrides kNoOverrides = {
  kUnsetFloat, // waveTriggerBeatScale
  kUnsetFloat, // waveTriggerFallbackScale
  kUnsetFloat, // brightnessPulseScale
  kUnsetFloat, // noseWidthScale
  kUnsetFloat, // tailWidthScale
  kUnsetFloat, // waveTailRatioScale
  kUnsetFloat, // waveNoseRatioScale
  kUnsetFloat, // waveSpeedBaseScale
  kUnsetFloat, // waveSpeedRangeScale
  kUnsetFloat, // waveHueDriftRoundsScale
};

// Animation behavior table.
// Fill any field by setting hasValue=true; leave as kUnset* to use runtime config.
static const AnimationConfigTable kAnimationConfigTable = {
  /* beatDetected  */ kNoOverrides,
  /* waitFadeStart */ kNoOverrides,
  /* waitFadeEnd   */ kNoOverrides,
  /* fadeStart     */ kNoOverrides,
  /* fadeEnd       */ kNoOverrides,
  /* defaults      */ kNoOverrides,
};

const AnimationConfigTable& getAnimationConfigTable() {
  return kAnimationConfigTable;
}

static inline float resolveFloatTransition(OptionalFloatValue start,
                                           OptionalFloatValue end,
                                           float progress,
                                           float fallbackValue) {
  progress = clamp01(progress);
  if (start.hasValue && end.hasValue) {
    return lerpf(start.value, end.value, progress);
  }
  if (start.hasValue) return start.value;
  if (end.hasValue) return end.value;
  return fallbackValue;
}

static float resolveScaleForPhase(const AnimationEffectContext& context,
                                  OptionalFloatValue defaults,
                                  OptionalFloatValue beat,
                                  OptionalFloatValue waitStart,
                                  OptionalFloatValue waitEnd,
                                  OptionalFloatValue fadeStart,
                                  OptionalFloatValue fadeEnd) {
  float scale = defaults.hasValue ? defaults.value : 1.0f;

  if (context.beatDetected) {
    if (beat.hasValue) scale = beat.value;
    return clamp01(scale);
  }

  if (context.inFadePhase) {
    return clamp01(resolveFloatTransition(fadeStart, fadeEnd, context.fadeProgress, scale));
  }

  if (context.inWaitPhase) {
    return clamp01(resolveFloatTransition(waitStart, waitEnd, context.waitProgress, scale));
  }

  return clamp01(scale);
}

void resolveAnimationEffects(const AnimationEffectContext& context,
                             const RuntimeConfig& baseConfig,
                             ResolvedAnimationEffects* out) {
  if (!out) return;

  float baseNose = clamp01(baseConfig.waveNoseRatio);
  float baseGap = clamp01(baseConfig.waveGapRatio);
  const float used = baseNose + baseGap;
  if (used > 1.0f) {
    const float inv = 1.0f / used;
    baseNose *= inv;
    baseGap *= inv;
  }
  float baseTail = 1.0f - (baseNose + baseGap);
  if (baseTail < 0.0f) baseTail = 0.0f;

  const AnimationConfigTable& table = getAnimationConfigTable();
  const float triggerBeatScale = resolveScaleForPhase(
    context,
    table.defaults.waveTriggerBeatScale,
    table.beatDetected.waveTriggerBeatScale,
    table.waitFadeStart.waveTriggerBeatScale,
    table.waitFadeEnd.waveTriggerBeatScale,
    table.fadeStart.waveTriggerBeatScale,
    table.fadeEnd.waveTriggerBeatScale
  );
  const float triggerFallbackScale = resolveScaleForPhase(
    context,
    table.defaults.waveTriggerFallbackScale,
    table.beatDetected.waveTriggerFallbackScale,
    table.waitFadeStart.waveTriggerFallbackScale,
    table.waitFadeEnd.waveTriggerFallbackScale,
    table.fadeStart.waveTriggerFallbackScale,
    table.fadeEnd.waveTriggerFallbackScale
  );
  const float brightnessPulseScale = resolveScaleForPhase(
    context,
    table.defaults.brightnessPulseScale,
    table.beatDetected.brightnessPulseScale,
    table.waitFadeStart.brightnessPulseScale,
    table.waitFadeEnd.brightnessPulseScale,
    table.fadeStart.brightnessPulseScale,
    table.fadeEnd.brightnessPulseScale
  );
  const float noseWidthScale = resolveScaleForPhase(
    context,
    table.defaults.noseWidthScale,
    table.beatDetected.noseWidthScale,
    table.waitFadeStart.noseWidthScale,
    table.waitFadeEnd.noseWidthScale,
    table.fadeStart.noseWidthScale,
    table.fadeEnd.noseWidthScale
  );
  const float tailWidthScale = resolveScaleForPhase(
    context,
    table.defaults.tailWidthScale,
    table.beatDetected.tailWidthScale,
    table.waitFadeStart.tailWidthScale,
    table.waitFadeEnd.tailWidthScale,
    table.fadeStart.tailWidthScale,
    table.fadeEnd.tailWidthScale
  );
  const float waveTailRatioScale = resolveScaleForPhase(
    context,
    table.defaults.waveTailRatioScale,
    table.beatDetected.waveTailRatioScale,
    table.waitFadeStart.waveTailRatioScale,
    table.waitFadeEnd.waveTailRatioScale,
    table.fadeStart.waveTailRatioScale,
    table.fadeEnd.waveTailRatioScale
  );
  const float waveNoseRatioScale = resolveScaleForPhase(
    context,
    table.defaults.waveNoseRatioScale,
    table.beatDetected.waveNoseRatioScale,
    table.waitFadeStart.waveNoseRatioScale,
    table.waitFadeEnd.waveNoseRatioScale,
    table.fadeStart.waveNoseRatioScale,
    table.fadeEnd.waveNoseRatioScale
  );
  const float waveSpeedBaseScale = resolveScaleForPhase(
    context,
    table.defaults.waveSpeedBaseScale,
    table.beatDetected.waveSpeedBaseScale,
    table.waitFadeStart.waveSpeedBaseScale,
    table.waitFadeEnd.waveSpeedBaseScale,
    table.fadeStart.waveSpeedBaseScale,
    table.fadeEnd.waveSpeedBaseScale
  );
  const float waveSpeedRangeScale = resolveScaleForPhase(
    context,
    table.defaults.waveSpeedRangeScale,
    table.beatDetected.waveSpeedRangeScale,
    table.waitFadeStart.waveSpeedRangeScale,
    table.waitFadeEnd.waveSpeedRangeScale,
    table.fadeStart.waveSpeedRangeScale,
    table.fadeEnd.waveSpeedRangeScale
  );
  const float waveHueDriftScale = resolveScaleForPhase(
    context,
    table.defaults.waveHueDriftRoundsScale,
    table.beatDetected.waveHueDriftRoundsScale,
    table.waitFadeStart.waveHueDriftRoundsScale,
    table.waitFadeEnd.waveHueDriftRoundsScale,
    table.fadeStart.waveHueDriftRoundsScale,
    table.fadeEnd.waveHueDriftRoundsScale
  );

  out->beatWaveTriggerScale = triggerBeatScale;
  out->fallbackWaveTriggerScale = triggerFallbackScale;
  out->enableBeatWaves = baseConfig.enableBeatWaves && (triggerBeatScale > 0.0f);
  out->enableFallbackWaves = baseConfig.enableFallbackWaves && (triggerFallbackScale > 0.0f);

  const float basePulseMin = clamp01(baseConfig.beatPulseMinRatio);
  const float baseWidthMin = clamp01(baseConfig.beatWidthMinRatio);
  out->brightnessPulseMinRatio = 1.0f - ((1.0f - basePulseMin) * brightnessPulseScale);
  out->noseWidthScaleMin = 1.0f - ((1.0f - baseWidthMin) * noseWidthScale);
  out->tailWidthScaleMin = 1.0f - ((1.0f - baseWidthMin) * tailWidthScale);

  out->waveTailRatio = baseTail * waveTailRatioScale;
  out->waveNoseRatio = baseNose * waveNoseRatioScale;
  out->waveSpeedBase = clampf(baseConfig.waveSpeedBase, 0.0f, 10.0f) * waveSpeedBaseScale;
  out->waveSpeedRange = clamp01(baseConfig.waveSpeedRange) * waveSpeedRangeScale;
  float baseHueRounds = (float)WAVE_HUE_DRIFT_ROUNDS;
  if (baseHueRounds < 0.0f) baseHueRounds = -baseHueRounds;
  out->waveHueDriftRounds = clampf(baseHueRounds, 0.0f, 32.0f) * waveHueDriftScale;

  const float ratioUsed = out->waveNoseRatio + out->waveTailRatio;
  if (ratioUsed > 1.0f) {
    const float inv = 1.0f / ratioUsed;
    out->waveNoseRatio *= inv;
    out->waveTailRatio *= inv;
  }
}
