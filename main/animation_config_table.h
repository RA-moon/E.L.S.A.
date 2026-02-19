#pragma once

#include "runtime_config.h"

struct OptionalFloatValue {
  bool hasValue;
  float value;
};

struct AnimationEffectOverrides {
  // All values are relative scales in [0..1]:
  // 1.0 = full config-defined effect, 0.0 = effect disabled, 0.5 = half effect.
  OptionalFloatValue waveTriggerBeatScale;
  OptionalFloatValue waveTriggerFallbackScale;
  OptionalFloatValue brightnessPulseScale;
  OptionalFloatValue noseWidthScale;
  OptionalFloatValue tailWidthScale;
  OptionalFloatValue waveTailRatioScale;
  OptionalFloatValue waveNoseRatioScale;
  OptionalFloatValue waveSpeedBaseScale;
  OptionalFloatValue waveSpeedRangeScale;
  OptionalFloatValue waveHueDriftRoundsScale;
};

// Table requested by user:
//   beat detected
//   wait to fade start/end
//   fade start/end
//   default values
//
// Any field with hasValue=false falls back to runtime config values.
struct AnimationConfigTable {
  AnimationEffectOverrides beatDetected;
  AnimationEffectOverrides waitFadeStart;
  AnimationEffectOverrides waitFadeEnd;
  AnimationEffectOverrides fadeStart;
  AnimationEffectOverrides fadeEnd;
  AnimationEffectOverrides defaults;
};

struct AnimationEffectContext {
  bool beatDetected;
  bool inWaitPhase;
  bool inFadePhase;
  float waitProgress; // 0..1 while waiting for fade start
  float fadeProgress; // 0..1 during fade
};

struct ResolvedAnimationEffects {
  bool enableBeatWaves;
  bool enableFallbackWaves;
  float beatWaveTriggerScale;
  float fallbackWaveTriggerScale;
  float brightnessPulseMinRatio;
  float noseWidthScaleMin;
  float tailWidthScaleMin;
  float waveTailRatio;
  float waveNoseRatio;
  float waveSpeedBase;
  float waveSpeedRange;
  float waveHueDriftRounds;
};

const AnimationConfigTable& getAnimationConfigTable();
void resolveAnimationEffects(const AnimationEffectContext& context,
                             const RuntimeConfig& baseConfig,
                             ResolvedAnimationEffects* out);
