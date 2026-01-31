#include "waveform.h"
#include <math.h>

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

  // Smoothstep-style falloff (smooth at edges).
  const float t = 1.0f - x;      // 1 at center, 0 at edge
  return t * t * (3.0f - 2.0f * t);
}
