#pragma once

// Audio interface for SPH0645 I2S microphone + simple FFT beat detection.
//
// - consumeBeat(): edge-triggered beat events (optionally returns strength 0..1)
// - getAverageBeatIntervalMs(): exponential moving average of beat interval (ms)
// - getAverageBpm(): convenience value derived from the average interval

// Legacy/global pulse multiplier used by the LED engine.
// (Kept for compatibility with the existing frame brightness logic.)
extern float brightnessPulse;

void setupI2S();
void processAudio();

// Beat detection event from FFT analysis.
// Returns true once per detected beat (edge triggered).
bool consumeBeat(float* strength = nullptr);

// Average time between detected beats (milliseconds).
float getAverageBeatIntervalMs();

// Convenience: 60000 / average beat interval.
float getAverageBpm();
