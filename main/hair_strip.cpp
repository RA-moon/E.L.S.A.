#include "hair_strip.h"
#include "elsa_config.h"

#include "esp_random.h"
#include <algorithm>

static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline int random_int(int min_val, int max_val) {
  if (max_val <= min_val) return min_val;
  const uint32_t span = (uint32_t)(max_val - min_val);
  return min_val + (int)(esp_random() % span);
}

static uint8_t s_hairHueOffset = 0;
static int s_hairFadeBrightness = 0;
static int s_hairFadeDirection = 1;
static uint32_t s_hairColorCycleStartMs = 0;
static uint32_t s_lastHairUpdateMs = 0;

void updateHairStrip(uint32_t nowMs, Rgb* leds, int ledCount) {
  if (!leds || ledCount <= 0) return;
  if ((int32_t)(nowMs - s_lastHairUpdateMs) < (int32_t)HAIR_UPDATE_MS) return;
  s_lastHairUpdateMs = nowMs;

  fill_solid(leds, ledCount, {0, 0, 0});

  const int lastIndex = ledCount - 1;
  const int r1Start = 0;
  const int r1End = std::min((int)HAIR_RAINBOW_END1, lastIndex);
  const int r2Start = std::max((int)HAIR_RAINBOW_START2, 0);
  const int r2End = std::min((int)HAIR_RAINBOW_END2, lastIndex);

  int activeRainbowLeds = 0;
  if (r1End >= r1Start) activeRainbowLeds += (r1End - r1Start + 1);
  if (r2End >= r2Start) activeRainbowLeds += (r2End - r2Start + 1);

  int rainbowIndex = 0;
  if (activeRainbowLeds > 0) {
    for (int i = r1Start; i <= r1End; i++) {
      const uint8_t hue = s_hairHueOffset + (uint8_t)(rainbowIndex * 255 / activeRainbowLeds);
      hsv_to_rgb(hue, 255, 255, &leds[i]);
      rainbowIndex++;
    }
    for (int i = r2Start; i <= r2End; i++) {
      const uint8_t hue = s_hairHueOffset + (uint8_t)(rainbowIndex * 255 / activeRainbowLeds);
      hsv_to_rgb(hue, 255, 255, &leds[i]);
      rainbowIndex++;
    }
  }

  if (s_hairColorCycleStartMs == 0) {
    s_hairColorCycleStartMs = nowMs;
  }
  const uint32_t elapsed = (nowMs - s_hairColorCycleStartMs) % HAIR_COLOR_CYCLE_DURATION_MS;
  const float phase = (float)elapsed / (HAIR_COLOR_CYCLE_DURATION_MS / 2.0f);
  const float interpFactor = (phase <= 1.0f) ? phase : (2.0f - phase);

  const int startHue = 96;
  const int endHue = 160;
  const uint8_t interpHue = (uint8_t)(startHue + (int)((endHue - startHue) * interpFactor));

  int effectiveBrightness = s_hairFadeBrightness;
  if (s_hairFadeBrightness > 80 && s_hairFadeBrightness < 180) {
    effectiveBrightness += random_int(-30, 30);
    effectiveBrightness = (int)clampf((float)effectiveBrightness, 0.0f, 255.0f);
  }
  Rgb fadeColor;
  hsv_to_rgb(interpHue, 255, (uint8_t)effectiveBrightness, &fadeColor);

  const int fStart = std::max((int)HAIR_FADE_START, 0);
  const int fEnd = std::min((int)HAIR_FADE_END, lastIndex);
  if (fEnd >= fStart) {
    for (int i = fStart; i <= fEnd; i++) {
      leds[i] = fadeColor;
    }
  }

  if (HAIR_BRIGHTNESS < 255) {
    scale_rgb(leds, ledCount, (uint8_t)HAIR_BRIGHTNESS);
  }

  s_hairHueOffset = (uint8_t)(s_hairHueOffset + HAIR_SPEED_RAINBOW);
  s_hairFadeBrightness += s_hairFadeDirection * HAIR_SPEED_FADE;
  if (s_hairFadeBrightness >= 255) {
    s_hairFadeBrightness = 255;
    s_hairFadeDirection = -1;
  } else if (s_hairFadeBrightness <= 0) {
    s_hairFadeBrightness = 0;
    s_hairFadeDirection = 1;
  }
}
