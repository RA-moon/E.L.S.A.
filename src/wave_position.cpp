#include "wave_position.h"
#include <algorithm>
#include <math.h>

static std::vector<Wave> waves;
static int gFrameCount = 10; // default; will be updated from the active animation
static uint32_t s_lastUpdateMs = 0;
static float s_waveSpeedBaseFps = 60.0f;

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

void dropOldestWave() {
  if (waves.empty()) return;
  waves.erase(waves.begin());
}

void applyWaveSpacing(float mix, float minNose, float maxNose) {
  if (waves.size() < 2) return;
  if (mix <= 0.0f) return;
  if (mix > 1.0f) mix = 1.0f;
  if (minNose < 0.001f) minNose = 0.001f;
  if (maxNose < minNose) maxNose = minNose;

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

  if (forward.size() >= 2) {
    sortByCenter(forward);
    // Leader is the wave with the higher center (moving forward).
    for (size_t i = forward.size() - 1; i > 0; i--) {
      const int leader = forward[i];
      const int follower = forward[i - 1];
      const float distance = waves[leader].center - waves[follower].center;
      float targetNose = distance - waves[leader].tailWidth;
      if (targetNose < minNose) targetNose = minNose;
      if (targetNose > maxNose) targetNose = maxNose;
      float next = (waves[follower].noseWidth * (1.0f - mix)) + (targetNose * mix);
      if (next < minNose) next = minNose;
      if (next > maxNose) next = maxNose;
      waves[follower].noseWidth = next;
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
      if (targetNose < minNose) targetNose = minNose;
      if (targetNose > maxNose) targetNose = maxNose;
      float next = (waves[follower].noseWidth * (1.0f - mix)) + (targetNose * mix);
      if (next < minNose) next = minNose;
      if (next > maxNose) next = maxNose;
      waves[follower].noseWidth = next;
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
  const float speedPerSec = speed * s_waveSpeedBaseFps;

  const float maxIndex = maxFrameIndex();

  Wave w;
  w.center = reverse ? (maxIndex + nose) : -tail;
  w.speed = reverse ? -speedPerSec : speedPerSec;
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
