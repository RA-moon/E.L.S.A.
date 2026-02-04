
#pragma once
#include <vector>
#include <Adafruit_NeoPixel.h>

void renderInterpolatedFrame(
  const std::vector<std::vector<int>>& frames,
  float waveCenter,
  uint32_t baseHue,
  float widthBehind,
  float widthAhead,
  int brightness,
  bool reverse,
  Adafruit_NeoPixel& strip,
  int ledCount
);
