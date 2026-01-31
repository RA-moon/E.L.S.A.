#include "wave_position.h"
#include <algorithm>

static std::vector<Wave> waves;
static int gFrameCount = 10; // default; will be updated from the active animation

void setWaveFrameCount(int frameCount) {
  if (frameCount <= 0) return;
  gFrameCount = frameCount;
}

static float maxFrameIndex() {
  return (gFrameCount > 0) ? float(gFrameCount - 1) : 0.0f;
}

void resetWaves() {
  waves.clear();
}

void updateWaves() {
  for (auto& wave : waves) {
    wave.center += wave.speed;
  }

  const float maxIndex = maxFrameIndex();

  waves.erase(
    std::remove_if(waves.begin(), waves.end(), [maxIndex](const Wave& wave) {
      return (!wave.reverse && wave.center > maxIndex + wave.noseWidth + 1.0f) ||
             (wave.reverse && wave.center < -wave.tailWidth - 1.0f);
    }),
    waves.end()
  );
}

const std::vector<Wave>& getWaves() {
  return waves;
}

void addWave(uint32_t hue, int8_t speedControl, float nose, float tail, bool reverse) {
  float speed = 0.2f + (speedControl / 25.0f);
  if (speed < 0.1f) speed = 0.1f;
  if (speed > 0.6f) speed = 0.6f;

  const float maxIndex = maxFrameIndex();

  Wave w;
  w.center = reverse ? (maxIndex + nose) : -tail;
  w.speed = reverse ? -speed : speed;
  w.hue = hue;
  w.noseWidth = nose;
  w.tailWidth = tail;
  w.totalWidth = nose + tail;
  w.reverse = reverse;
  waves.push_back(w);
}
