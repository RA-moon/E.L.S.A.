#pragma once

#include <stdint.h>

#include "beat_scheduler_policy.h"
#include "led_utils.h"

struct AnimationSchedulerTelemetry {
  uint32_t lastSpawnMs;
  uint32_t lastFallbackIntervalMs;
  WaveSpawnReason lastSpawnReason;
};

void animationEngineInit(Rgb* leds, uint16_t ledCount);
void animationEngineSetBuffer(Rgb* leds);
void animationEngineReset(uint32_t nowMs);
void runLedAnimation(uint32_t nowMs);
void getAnimationSchedulerTelemetry(AnimationSchedulerTelemetry* out);
