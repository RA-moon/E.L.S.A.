#include "led_utils.h"

void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, Rgb* out) {
  if (!out) return;
  if (s == 0) {
    out->r = v;
    out->g = v;
    out->b = v;
    return;
  }
  const uint8_t region = h / 43;
  const uint8_t remainder = (uint8_t)((h - (region * 43)) * 6);

  const uint8_t p = (uint8_t)((v * (255 - s)) >> 8);
  const uint8_t q = (uint8_t)((v * (255 - ((s * remainder) >> 8))) >> 8);
  const uint8_t t = (uint8_t)((v * (255 - ((s * (255 - remainder)) >> 8))) >> 8);

  switch (region) {
    case 0:
      out->r = v; out->g = t; out->b = p; break;
    case 1:
      out->r = q; out->g = v; out->b = p; break;
    case 2:
      out->r = p; out->g = v; out->b = t; break;
    case 3:
      out->r = p; out->g = q; out->b = v; break;
    case 4:
      out->r = t; out->g = p; out->b = v; break;
    default:
      out->r = v; out->g = p; out->b = q; break;
  }
}

void fill_solid(Rgb* leds, int count, Rgb color) {
  if (!leds || count <= 0) return;
  for (int i = 0; i < count; i++) {
    leds[i] = color;
  }
}

void scale_rgb(Rgb* leds, int count, uint8_t scale) {
  if (!leds || count <= 0) return;
  if (scale >= 255) return;
  for (int i = 0; i < count; i++) {
    const uint16_t r = (uint16_t)leds[i].r * scale;
    const uint16_t g = (uint16_t)leds[i].g * scale;
    const uint16_t b = (uint16_t)leds[i].b * scale;
    leds[i].r = (uint8_t)((r + 127) / 255);
    leds[i].g = (uint8_t)((g + 127) / 255);
    leds[i].b = (uint8_t)((b + 127) / 255);
  }
}
