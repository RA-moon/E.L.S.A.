#include "frame_interpolation.h"
#include "waveform.h"
#include <math.h>

std::vector<FrameResult> getInterpolatedFrame(
  const std::vector<std::vector<int>>& frames,
  float waveCenter,
  uint32_t baseHue,
  float widthBehind,
  float widthAhead,
  int brightness,
  bool reverse
) {
  std::vector<FrameResult> result;
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
      if (led >= 0) {
        result.push_back({ led, color });
      }
    }
  }

  return result;
}
