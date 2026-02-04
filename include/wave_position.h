#pragma once
#include <stdint.h>
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
  bool reverse;
};

// The wave system moves across "frame indices" (not LED indices). Different
// animations can have different frame counts.
void setWaveFrameCount(int frameCount);

void resetWaves();
bool updateWaves(uint32_t nowMs);
const std::vector<Wave>& getWaves();
void dropOldestWave();
void applyWaveSpacing(float mix, float minNose, float maxNose);
void setWaveSpeedBaseFps(float fps);
void addWave(uint32_t hue,
             int8_t speedControl = 0,
             float nose = 1.0f,
             float tail = 3.0f,
             bool reverse = false,
             int16_t hueStartDeg = 0,
             int16_t hueEndDeg = 0);
