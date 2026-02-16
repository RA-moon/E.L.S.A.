#pragma once

#include <stdint.h>

struct Rgb {
  // Stored as GRB for WS2812 output buffers.
  uint8_t g;
  uint8_t r;
  uint8_t b;
};

void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, Rgb* out);
void fill_solid(Rgb* leds, int count, Rgb color);
void scale_rgb(Rgb* leds, int count, uint8_t scale);
