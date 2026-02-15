
#pragma once
#include <vector>
#include <FastLED.h>

void renderInterpolatedFrame(
  const std::vector<std::vector<int>>& frames,
  float waveCenter,
  uint32_t baseHue,
  float widthBehind,
  float widthAhead,
  int brightness,
  bool reverse,
  CRGB* leds,
  int ledCount
);
