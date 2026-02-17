#include "waveform.h"

#include "elsa_config.h"

#include <math.h>

static inline float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static inline float lerpf(float a, float b, float t) {
  return a + (b - a) * t;
}

static inline float bezierEase(float t, float p1, float p2) {
  t = clamp01(t);
  p1 = clamp01(p1);
  p2 = clamp01(p2);
  const float u = 1.0f - t;
  return (3.0f * u * u * t * p1) + (3.0f * u * t * t * p2) + (t * t * t);
}

struct FalloffCurve {
  float p1;
  float p2;
};

static FalloffCurve s_noseCurve = {WAVE_FALLOFF_NOSE_P1, WAVE_FALLOFF_NOSE_P2};
static FalloffCurve s_tailCurve = {WAVE_FALLOFF_TAIL_P1, WAVE_FALLOFF_TAIL_P2};

#if WAVE_FALLOFF_LUT_ENABLE
static float s_noseLut[WAVE_FALLOFF_LUT_SIZE];
static float s_tailLut[WAVE_FALLOFF_LUT_SIZE];
static bool s_lutReady = false;

static void buildLut(const FalloffCurve& curve, float* out) {
  const int last = (WAVE_FALLOFF_LUT_SIZE > 1) ? (WAVE_FALLOFF_LUT_SIZE - 1) : 1;
  for (int i = 0; i < WAVE_FALLOFF_LUT_SIZE; i++) {
    const float t = (float)i / (float)last;
    out[i] = bezierEase(t, curve.p1, curve.p2);
  }
}

static float sampleLut(const float* lut, float t) {
  t = clamp01(t);
  if (WAVE_FALLOFF_LUT_SIZE <= 1) return lut[0];
  const float idxf = t * (float)(WAVE_FALLOFF_LUT_SIZE - 1);
  const int idx = (int)idxf;
  if (idx >= (WAVE_FALLOFF_LUT_SIZE - 1)) return lut[WAVE_FALLOFF_LUT_SIZE - 1];
  const float frac = idxf - (float)idx;
  return lerpf(lut[idx], lut[idx + 1], frac);
}
#endif

void waveformSetFalloff(float noseP1, float noseP2, float tailP1, float tailP2) {
  s_noseCurve.p1 = clamp01(noseP1);
  s_noseCurve.p2 = clamp01(noseP2);
  s_tailCurve.p1 = clamp01(tailP1);
  s_tailCurve.p2 = clamp01(tailP2);
#if WAVE_FALLOFF_LUT_ENABLE
  buildLut(s_noseCurve, s_noseLut);
  buildLut(s_tailCurve, s_tailLut);
  s_lutReady = true;
#endif
}

void waveformInitFalloff() {
  waveformSetFalloff(WAVE_FALLOFF_NOSE_P1,
                     WAVE_FALLOFF_NOSE_P2,
                     WAVE_FALLOFF_TAIL_P1,
                     WAVE_FALLOFF_TAIL_P2);
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
  const bool isTail = (d < 0.0f);
  const float w = (d < 0.0f) ? widthBehind : widthAhead;

  if (w <= 0.0001f) return 0.0f;

  const float x = fabsf(d) / w;  // 0..inf
  if (x >= 1.0f) return 0.0f;

  // Bezier-style falloff (smooth at edges), same curve for tail and nose.
  const float t = 1.0f - x;      // 1 at center, 0 at edge
#if WAVE_FALLOFF_LUT_ENABLE
  if (!s_lutReady) waveformInitFalloff();
  const float* lut = isTail ? s_tailLut : s_noseLut;
  return sampleLut(lut, t);
#else
  const FalloffCurve& curve = isTail ? s_tailCurve : s_noseCurve;
  return bezierEase(t, curve.p1, curve.p2);
#endif
}
