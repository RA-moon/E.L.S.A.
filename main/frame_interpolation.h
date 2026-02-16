#pragma once

#include <stdint.h>
#include <vector>

#include "led_utils.h"

int renderInterpolatedFrame(
  const std::vector<std::vector<int>>& frames,
  float waveCenter,
  uint32_t baseHue,
  float widthBehind,
  float widthAhead,
  int brightness,
  bool reverse,
  Rgb* leds,
  int ledCount
);
