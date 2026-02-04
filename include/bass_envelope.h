#pragma once

#include <cstddef>
#include <cstdint>

// Bass envelope detector (no FFT).
// - Bandpass 55-160 Hz
// - Rectify + dual-EMA envelope (fast attack, slow release)
// - Adaptive baseline + hysteresis (thr_on / thr_off)
// - State machine: IDLE -> ATTACK -> RELEASE
// - Refractory to avoid double triggers
//
// Feed raw mono samples at the configured sample rate.
// Call processSamples() continuously; it will emit events with
// attack_ms and sustain_release_ms.

struct BassEnvelopeEvent {
  uint16_t attack_ms;
  uint16_t sustain_release_ms;
};

struct BassEnvelopeConfig {
  uint32_t sample_rate_hz = 16000;
  uint16_t update_ms = 10;

  // Envelope EMAs
  float attack_alpha = 0.55f;
  float release_alpha = 0.08f;

  // Baseline EMA (only in quiet phases)
  float baseline_alpha = 0.004f;

  // Thresholds relative to baseline
  float thr_on_mul = 2.0f;
  float thr_off_mul = 1.4f;

  // Refractory + release cap
  uint16_t refractory_ms = 260;
  uint16_t sr_cap_ms = 800;
};

class BassEnvelopeDetector {
public:
  explicit BassEnvelopeDetector(const BassEnvelopeConfig& cfg = BassEnvelopeConfig());

  void reset();
  void setConfig(const BassEnvelopeConfig& cfg);
  const BassEnvelopeConfig& getConfig() const { return cfg_; }

  // Process a block of signed 32-bit samples (mono).
  // Returns true if an event is emitted, and fills out_event.
  bool processSamples(const int32_t* samples, size_t count, uint32_t now_ms, BassEnvelopeEvent* out_event);

  // Process a single rectified envelope sample (e.g. from FFT bass energy).
  bool processEnvelope(float rectified, uint32_t now_ms, BassEnvelopeEvent* out_event);

private:
  struct Biquad {
    float b0, b1, b2, a1, a2;
    float z1 = 0.0f;
    float z2 = 0.0f;
    float process(float x) {
      const float y = b0 * x + z1;
      z1 = b1 * x + z2 - a1 * y;
      z2 = b2 * x - a2 * y;
      return y;
    }
  };

  void designBandpass();
  bool updateEnvelope(float rectified, uint32_t now_ms, BassEnvelopeEvent* out_event);

  BassEnvelopeConfig cfg_;
  Biquad bp_;

  uint32_t samples_until_update_ = 0;
  uint32_t window_samples_ = 0;
  float rect_sum_ = 0.0f;

  float env_ = 0.0f;
  float baseline_ = 0.0f;
  float prev_env_ = 0.0f;

  enum State { IDLE, ATTACK, RELEASE } state_ = IDLE;
  uint32_t attack_start_ms_ = 0;
  uint32_t peak_ms_ = 0;
  float peak_env_ = 0.0f;
  uint32_t last_event_ms_ = 0;
  uint32_t time_ms_ = 0;
  bool time_init_ = false;
};
