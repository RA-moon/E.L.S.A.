#include "wave_position.h"
#include <algorithm>
#include <math.h>

static std::vector<Wave> waves;
static int gFrameCount = 10; // default; will be updated from the active animation
static uint32_t s_lastUpdateMs = 0;
static float s_waveSpeedBaseFps = 60.0f;
static float s_waveSpeedBase = 0.01f;
static float s_waveSpeedRange = 1.0f;
static float s_waveSpeedMultiplier = 1.0f;

void setWaveFrameCount(int frameCount) {
  if (frameCount <= 0) return;
  gFrameCount = frameCount;
}

static float maxFrameIndex() {
  return (gFrameCount > 0) ? float(gFrameCount - 1) : 0.0f;
}

void resetWaves() {
  waves.clear();
  s_lastUpdateMs = 0;
}

void setWaveSpeedBaseFps(float fps) {
  if (fps < 1.0f) fps = 1.0f;
  if (fps > 240.0f) fps = 240.0f;
  s_waveSpeedBaseFps = fps;
}

void setWaveSpeedBase(float base) {
  if (base < 0.0f) base = 0.0f;
  if (base > 5.0f) base = 5.0f;
  s_waveSpeedBase = base;
}

void setWaveSpeedRange(float range) {
  if (range < 1.0f) range = 1.0f;
  if (range > 5.0f) range = 5.0f;
  s_waveSpeedRange = range;
}

void setWaveSpeedMultiplier(float multiplier) {
  if (multiplier < 0.1f) multiplier = 0.1f;
  if (multiplier > 5.0f) multiplier = 5.0f;
  s_waveSpeedMultiplier = multiplier;
}

bool updateWaves(uint32_t nowMs) {
  if (s_lastUpdateMs == 0) {
    s_lastUpdateMs = nowMs;
    return false;
  }
  uint32_t dtMs = nowMs - s_lastUpdateMs;
  if (dtMs == 0) return false;
  s_lastUpdateMs = nowMs;

  const float dt = (float)dtMs / 1000.0f;
  const float maxIndex = maxFrameIndex();
  for (auto& wave : waves) {
    wave.center += wave.speed * dt;

    const float endCenter = wave.reverse ? (-wave.tailWidth - 1.0f) : (maxIndex + wave.noseWidth + 1.0f);
    const float denom = endCenter - wave.startCenter;
    float progress = 1.0f;
    if (fabsf(denom) > 1e-3f) {
      progress = (wave.center - wave.startCenter) / denom;
    }
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    const float offset = (float)wave.hueStartOffset +
                         (float)(wave.hueEndOffset - wave.hueStartOffset) * progress;
    const int32_t newHue = (int32_t)wave.baseHue + (int32_t)lroundf(offset);
    wave.hue = (uint32_t)newHue & 0xFFFF;
  }

  waves.erase(
    std::remove_if(waves.begin(), waves.end(), [maxIndex](const Wave& wave) {
      return (!wave.reverse && wave.center > maxIndex + wave.noseWidth + 1.0f) ||
             (wave.reverse && wave.center < -wave.tailWidth - 1.0f);
    }),
    waves.end()
  );

  return true;
}

const std::vector<Wave>& getWaves() {
  return waves;
}

std::vector<Wave>& getWavesMutable() {
  return waves;
}

void dropOldestWave() {
  if (waves.empty()) return;
  waves.erase(waves.begin());
}

void addWave(uint32_t hue,
             int8_t speedControl,
             float nose,
             float tail,
             bool reverse,
             int32_t hueStartOffset,
             int32_t hueEndOffset) {
  const float ctl = (float)speedControl / 10.0f;
  float range = s_waveSpeedRange;
  if (range < 1.0f) range = 1.0f;
  const float mult = powf(range, ctl);
  float speed = s_waveSpeedBase * mult;
  if (speed < 0.0f) speed = 0.0f;
  const float speedPerSec = speed * s_waveSpeedBaseFps * s_waveSpeedMultiplier;

  const float maxIndex = maxFrameIndex();

  Wave w;
  const float startOutside = nose * 2.0f;
  w.center = reverse ? (maxIndex + nose) : -startOutside;
  w.speed = reverse ? -speedPerSec : speedPerSec;
  w.hue = hue;
  w.baseHue = hue;
  w.hueStartOffset = hueStartOffset;
  w.hueEndOffset = hueEndOffset;
  w.startCenter = w.center;
  w.noseWidth = nose;
  w.tailWidth = tail;
  w.reverse = reverse;
  waves.push_back(w);
}
