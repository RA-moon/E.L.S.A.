#pragma once

#include <cstdint>

struct BeatTelemetry {
  uint32_t beatCount;
  uint32_t lastBeatMs;
  float lastBeatStrength;
  float avgBeatIntervalMs;
  float bpm;
  uint32_t lastWaveMs;
  uint32_t lastWaveIntervalMs;
  uint32_t wavePeriodMs;
  uint32_t nextWaveInMs;
  uint32_t activeWaves;
  int animationIndex;
  const char* animationName;
  float baseBrightnessRatio;
  float pulseRatio;
};
