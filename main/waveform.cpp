#include "waveform.h"

#include "elsa_config.h"

#include <math.h>

static inline float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static inline float bezierEase(float t, float p1, float p2) {
  t = clamp01(t);
  p1 = clamp01(p1);
  p2 = clamp01(p2);
  const float u = 1.0f - t;
  return (3.0f * u * u * t * p1) + (3.0f * u * t * t * p2) + (t * t * t);
}

// Returns an intensity in [0..1] around "center", with different falloff widths
// behind (tail) and ahead (nose).
//
// frameIndex: current frame index (0..N-1)
// center:     wave center position (can be fractional)
// widthBehind: width on the negative side (tail)
// widthAhead:  width on the positive side (nose)
float getAsymmetricIntensity(float frameIndex, float center, float widthBehind, float widthAhead) {
  const float d = frameIndex - center;
  const float w = (d < 0.0f) ? widthBehind : widthAhead;

  if (w <= 0.0001f) return 0.0f;

  const float x = fabsf(d) / w;  // 0..inf
  if (x >= 1.0f) return 0.0f;

  // Bezier-style falloff (smooth at edges), same curve for tail and nose.
  const float t = 1.0f - x;      // 1 at center, 0 at edge
  return bezierEase(t, WAVE_FALLOFF_P1, WAVE_FALLOFF_P2);
}
