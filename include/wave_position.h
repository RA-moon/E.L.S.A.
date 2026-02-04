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
  float totalWidth;
  bool reverse;
};

// The wave system moves across "frame indices" (not LED indices). Different
// animations can have different frame counts.
void setWaveFrameCount(int frameCount);

void resetWaves();
void updateWaves();
const std::vector<Wave>& getWaves();
void dropOldestWave();
void applyWaveSpacing(float mix);
void addWave(uint32_t hue,
             int8_t speedControl = 0,
             float nose = 1.0f,
             float tail = 3.0f,
             bool reverse = false,
             int16_t hueStartDeg = 0,
             int16_t hueEndDeg = 0);
