#pragma once

#include <cstdint>

// Audio interface for SPH0645 I2S microphone + FFT beat detection.
//
// - consumeBeat(): edge-triggered beat events with source + strength metadata
// - getAverageBeatIntervalMs(): exponential moving average of beat interval (ms)
// - getAverageBpm(): convenience value derived from the average interval

void setupI2S();
void processAudio();

struct BeatEvent {
  float strength;      // 0..1
  uint32_t timestampMs;
  bool isReal;         // true = FFT-detected beat, false = synthetic/fake beat
};

// Beat event from audio processing (edge-triggered; consumed once).
// Returns true when an event is available and copies metadata to outEvent if provided.
bool consumeBeat(BeatEvent* outEvent = nullptr);

// Average time between detected beats (milliseconds).
float getAverageBeatIntervalMs();

// Timestamp of last real (detected) beat in ms (0 if none yet).
uint32_t getLastRealBeatMs();

// Convenience: 60000 / average beat interval.
float getAverageBpm();

// Snapshot of audio/FFT internals for telemetry and tuning.
struct AudioTelemetry {
  float bass;
  float bassEma;
  float ratio;
  float rise;
  float threshold;
  float riseThreshold;
  float micRms;
  float micPeak;
  float beatStrength;
  uint32_t lastBeatMs;
  uint32_t lastBeatIntervalMs;
  uint32_t sampleRateHz;
  uint16_t fftSamples;
  float bassMinHz;
  float bassMaxHz;
  uint16_t binMin;
  uint16_t binMax;
  float binWidthHz;
  bool intervalOk;
  bool above;
  bool rising;
  bool i2sOk;
};

struct BeatDetectorConfig {
  float energyEmaAlpha;
  float fluxEmaAlpha;
  float fluxThreshold;
  float fluxRiseFactor;
  uint32_t minBeatIntervalMs;
  uint32_t avgBeatMinMs;
  uint32_t avgBeatMaxMs;
};

void getAudioTelemetry(AudioTelemetry* out);
void getBeatDetectorConfig(BeatDetectorConfig* out);
void setBeatDetectorConfig(const BeatDetectorConfig* cfg);
