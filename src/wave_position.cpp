#include "wave_position.h"
#include <algorithm>
#include <math.h>

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
  const float maxIndex = maxFrameIndex();
  for (auto& wave : waves) {
    wave.center += wave.speed;

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
}

const std::vector<Wave>& getWaves() {
  return waves;
}

void dropOldestWave() {
  if (waves.empty()) return;
  waves.erase(waves.begin());
}

void applyWaveSpacing(float mix) {
  if (waves.size() < 2) return;
  if (mix <= 0.0f) return;
  if (mix > 1.0f) mix = 1.0f;

  std::vector<int> forward;
  std::vector<int> reverse;
  forward.reserve(waves.size());
  reverse.reserve(waves.size());
  for (size_t i = 0; i < waves.size(); i++) {
    if (waves[i].reverse) reverse.push_back((int)i);
    else forward.push_back((int)i);
  }

  auto sortByCenter = [&](std::vector<int>& idx) {
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
      return waves[a].center < waves[b].center;
    });
  };

  const float kMinNose = 0.05f;

  if (forward.size() >= 2) {
    sortByCenter(forward);
    // Leader is the wave with the higher center (moving forward).
    for (size_t i = forward.size() - 1; i > 0; i--) {
      const int leader = forward[i];
      const int follower = forward[i - 1];
      const float distance = waves[leader].center - waves[follower].center;
      float targetNose = distance - waves[leader].tailWidth;
      if (targetNose < kMinNose) targetNose = kMinNose;
      waves[follower].noseWidth = (waves[follower].noseWidth * (1.0f - mix)) + (targetNose * mix);
    }
  }

  if (reverse.size() >= 2) {
    sortByCenter(reverse);
    // Leader is the wave with the lower center (moving reverse).
    for (size_t i = 1; i < reverse.size(); i++) {
      const int leader = reverse[i - 1];
      const int follower = reverse[i];
      const float distance = waves[follower].center - waves[leader].center;
      float targetNose = distance - waves[leader].tailWidth;
      if (targetNose < kMinNose) targetNose = kMinNose;
      waves[follower].noseWidth = (waves[follower].noseWidth * (1.0f - mix)) + (targetNose * mix);
    }
  }
}

static int32_t hueOffsetFromDegrees(int16_t deg) {
  if (deg == 0) return 0;
  if (deg > 360) deg = 360;
  if (deg < -360) deg = -360;
  const float scale = 65535.0f / 360.0f;
  return (int32_t)lroundf((float)deg * scale);
}

void addWave(uint32_t hue,
             int8_t speedControl,
             float nose,
             float tail,
             bool reverse,
             int16_t hueStartDeg,
             int16_t hueEndDeg) {
  float speed = 0.2f + (speedControl / 25.0f);
  if (speed < 0.1f) speed = 0.1f;
  if (speed > 0.6f) speed = 0.6f;

  const float maxIndex = maxFrameIndex();

  Wave w;
  w.center = reverse ? (maxIndex + nose) : -tail;
  w.speed = reverse ? -speed : speed;
  w.hue = hue;
  w.baseHue = hue;
  w.hueStartOffset = hueOffsetFromDegrees(hueStartDeg);
  w.hueEndOffset = hueOffsetFromDegrees(hueEndDeg);
  w.startCenter = w.center;
  w.noseWidth = nose;
  w.tailWidth = tail;
  w.totalWidth = nose + tail;
  w.reverse = reverse;
  waves.push_back(w);
}
