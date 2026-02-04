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
  Adafruit_NeoPixel& strip,
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

    const uint32_t color = Adafruit_NeoPixel::ColorHSV((uint16_t)baseHue, 255, (uint8_t)v);

    for (int led : frames[waveFrameIndex]) {
      if (led >= 0 && led < ledCount) {
        strip.setPixelColor((uint16_t)led, color);
      }
    }
  }
}
