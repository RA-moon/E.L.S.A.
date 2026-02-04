#include "bass_envelope.h"
#include <math.h>

static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

BassEnvelopeDetector::BassEnvelopeDetector(const BassEnvelopeConfig& cfg) : cfg_(cfg) {
  reset();
}

void BassEnvelopeDetector::reset() {
  bp_ = {};
  designBandpass();
  samples_until_update_ = (cfg_.sample_rate_hz * cfg_.update_ms) / 1000;
  if (samples_until_update_ == 0) samples_until_update_ = 1;
  rect_sum_ = 0.0f;
  env_ = 0.0f;
  baseline_ = 0.0f;
  prev_env_ = 0.0f;
  state_ = IDLE;
  attack_start_ms_ = 0;
  peak_ms_ = 0;
  peak_env_ = 0.0f;
  last_event_ms_ = 0;
  time_ms_ = 0;
  time_init_ = false;
}

void BassEnvelopeDetector::setConfig(const BassEnvelopeConfig& cfg) {
  cfg_ = cfg;
  reset();
}

void BassEnvelopeDetector::designBandpass() {
  // RBJ biquad bandpass (constant skirt gain).
  const float fs = (float)cfg_.sample_rate_hz;
  const float f1 = 55.0f;
  const float f2 = 160.0f;
  const float fc = sqrtf(f1 * f2);
  const float q = fc / (f2 - f1);

  const float w0 = 2.0f * (float)M_PI * (fc / fs);
  const float cosw0 = cosf(w0);
  const float sinw0 = sinf(w0);
  const float alpha = sinw0 / (2.0f * q);

  const float b0 = alpha;
  const float b1 = 0.0f;
  const float b2 = -alpha;
  const float a0 = 1.0f + alpha;
  const float a1 = -2.0f * cosw0;
  const float a2 = 1.0f - alpha;

  bp_.b0 = b0 / a0;
  bp_.b1 = b1 / a0;
  bp_.b2 = b2 / a0;
  bp_.a1 = a1 / a0;
  bp_.a2 = a2 / a0;
}

bool BassEnvelopeDetector::processSamples(const int32_t* samples,
                                          size_t count,
                                          uint32_t now_ms,
                                          BassEnvelopeEvent* out_event) {
  if (!samples || count == 0) return false;

  if (!time_init_) {
    time_ms_ = now_ms;
    time_init_ = true;
  } else if (now_ms > time_ms_ + 1000) {
    // Resync if caller time jumps (or we fell behind).
    time_ms_ = now_ms;
  }

  bool fired = false;
  for (size_t i = 0; i < count; i++) {
    // Normalize to roughly [-1, 1] from 24-bit PCM in 32-bit container.
    const float x = (float)samples[i] / 8388608.0f;
    const float bp = bp_.process(x);
    const float rect = fabsf(bp);

    rect_sum_ += rect;
    if (--samples_until_update_ == 0) {
      samples_until_update_ = (cfg_.sample_rate_hz * cfg_.update_ms) / 1000;
      if (samples_until_update_ == 0) samples_until_update_ = 1;

      const float rectified = rect_sum_ / (float)samples_until_update_;
      rect_sum_ = 0.0f;

      if (updateEnvelope(rectified, time_ms_, out_event)) {
        fired = true;
      }
      time_ms_ += cfg_.update_ms;
    }
  }
  return fired;
}

bool BassEnvelopeDetector::updateEnvelope(float rectified,
                                          uint32_t now_ms,
                                          BassEnvelopeEvent* out_event) {
  // Dual-EMA envelope
  if (rectified > env_) {
    env_ = (1.0f - cfg_.attack_alpha) * env_ + (cfg_.attack_alpha * rectified);
  } else {
    env_ = (1.0f - cfg_.release_alpha) * env_ + (cfg_.release_alpha * rectified);
  }

  // Adaptive baseline (only in quiet phases, but initialize on first run)
  if (baseline_ <= 0.000001f) {
    baseline_ = env_;
  } else if (env_ < baseline_ * 1.1f) {
    baseline_ = (1.0f - cfg_.baseline_alpha) * baseline_ + (cfg_.baseline_alpha * env_);
  }

  const float thr_on = baseline_ * cfg_.thr_on_mul;
  const float thr_off = baseline_ * cfg_.thr_off_mul;

  const bool refractory_ok = (now_ms - last_event_ms_) >= cfg_.refractory_ms;
  bool fired = false;

  switch (state_) {
    case IDLE:
      if (refractory_ok && env_ > thr_on) {
        state_ = ATTACK;
        attack_start_ms_ = now_ms;
        peak_ms_ = now_ms;
        peak_env_ = env_;
      }
      break;
    case ATTACK:
      if (env_ >= peak_env_) {
        peak_env_ = env_;
        peak_ms_ = now_ms;
      }
      if (env_ < prev_env_) {
        // Peak detected
        state_ = RELEASE;
      }
      break;
    case RELEASE: {
      const uint32_t sr_ms = now_ms - peak_ms_;
      if (env_ <= thr_off || sr_ms >= cfg_.sr_cap_ms) {
        const uint32_t attack_ms = peak_ms_ - attack_start_ms_;
        if (out_event) {
          out_event->attack_ms = (uint16_t)clampf((float)attack_ms, 0.0f, 10000.0f);
          out_event->sustain_release_ms = (uint16_t)clampf((float)sr_ms, 0.0f, 10000.0f);
        }
        last_event_ms_ = now_ms;
        state_ = IDLE;
        fired = true;
      }
      break;
    }
  }

  prev_env_ = env_;
  return fired;
}
