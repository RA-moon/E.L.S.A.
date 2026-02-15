#include "frame_interpolation.h"
#include "waveform.h"
#include <math.h>

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
) {
  const int totalRings = (int)frames.size();

  for (int i = 0; i < totalRings; i++) {
    const int waveFrameIndex = i;
    const int brightnessFrameIndex = reverse ? (totalRings - 1 - i) : i;

    const float actualTail = reverse ? widthAhead : widthBehind;
    const float actualNose = reverse ? widthBehind : widthAhead;

    const float intensity = getAsymmetricIntensity((float)brightnessFrameIndex, waveCenter, actualTail, actualNose);
    if (intensity < 0.005f) continue;

    int v = (int)lroundf((float)brightness * intensity);
    if (v < 0) v = 0;
    if (v > 255) v = 255;

    const uint8_t hue8 = (uint8_t)(baseHue >> 8);
    const CHSV color(hue8, 255, (uint8_t)v);

    for (int led : frames[waveFrameIndex]) {
      if (led >= 0 && led < ledCount) {
        leds[led] = color;
      }
    }
  }
}
