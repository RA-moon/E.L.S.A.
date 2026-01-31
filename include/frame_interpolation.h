
#pragma once
#include <vector>
#include <Adafruit_NeoPixel.h>

struct FrameResult {
  int ledIndex;
  uint32_t color;
};

std::vector<FrameResult> getInterpolatedFrame(
  const std::vector<std::vector<int>>& frames,
  float waveCenter,
  uint32_t baseHue,
  float widthBehind,
  float widthAhead,
  int brightness,
  bool reverse
);
