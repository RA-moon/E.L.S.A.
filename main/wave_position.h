#pragma once

#include <cstdint>
#include <vector>

struct Wave {
  float center;
  float speed;
  uint32_t hue;
  uint32_t baseHue;
  int32_t hueStartOffset;
  int32_t hueEndOffset;
  float startCenter;
  float noseWidth;
  float tailWidth;
};

// The wave system moves across "frame indices" (not LED indices). Different
// animations can have different frame counts.
void setWaveFrameCount(int frameCount);

void resetWaves();
bool updateWaves(uint32_t nowMs);
const std::vector<Wave>& getWaves();
std::vector<Wave>& getWavesMutable();
void dropOldestWave();
void setWaveSpeedBaseFps(float fps);
void setWaveSpeedBase(float base);
void setWaveSpeedRange(float range);
void setWaveSpeedMultiplier(float multiplier);
void addWave(uint32_t hue,
             int8_t speedControl = 0,
             float nose = 1.0f,
             float tail = 1.0f,
             int32_t hueStartOffset = 0,
             int32_t hueEndOffset = 0);
