#include "audio_processor.h"
#include <Arduino.h>

// I2S API (ESP32 core 3.x uses ESP_I2S.h; older cores provide I2S.h)
#define HAS_ESP_I2S 0
#define HAS_ARDUINO_I2S 0
#if defined(__has_include)
#if __has_include(<ESP_I2S.h>)
#include <ESP_I2S.h>
#undef HAS_ESP_I2S
#define HAS_ESP_I2S 1
#elif __has_include(<I2S.h>)
#include <I2S.h>
#undef HAS_ARDUINO_I2S
#define HAS_ARDUINO_I2S 1
#endif
#else
#include <ESP_I2S.h>
#undef HAS_ESP_I2S
#define HAS_ESP_I2S 1
#endif

// ESP-DSP FFT
#include "esp_dsp.h"
#include "dsps_wind_hann.h"
#include <math.h>

// Optional bass envelope detector (no FFT).
#ifndef BASS_ENVELOPE_ENABLE
#define BASS_ENVELOPE_ENABLE 1
#endif

// Disable time-domain envelope (keep FFT-driven envelope only).
#ifndef BASS_ENVELOPE_TIME_DOMAIN
#define BASS_ENVELOPE_TIME_DOMAIN 0
#endif

#if BASS_ENVELOPE_ENABLE
#include "bass_envelope.h"
#endif
// === Compile-time switches ===
#ifndef AUDIO_ENABLE_I2S
#define AUDIO_ENABLE_I2S 1
#endif

// === I2S pin mapping (adjust to your wiring) ===
// Recommended: keep to "safe" GPIOs on your board and avoid pins already used for LEDs.
#ifndef I2S_BCLK_PIN
#define I2S_BCLK_PIN 5
#endif
#ifndef I2S_WS_PIN
#define I2S_WS_PIN 6
#endif
#ifndef I2S_DIN_PIN
#define I2S_DIN_PIN 7
#endif
#ifndef I2S_MCLK_PIN
#define I2S_MCLK_PIN -1
#endif

// === SPH0645 settings ===
// The SPH0645 drives DATA on either the left slot (WS=0) or right slot (WS=1),
// depending on SEL. SEL=LOW -> left slot, SEL=HIGH -> right slot.
#ifndef SPH0645_CHANNEL
#define SPH0645_CHANNEL 0 // 0=left, 1=right
#endif

// SPH0645 outputs 24-bit samples in a 32-bit I2S slot (8 padding bits). Shifting by 8
// usually yields a signed 24-bit value in a 32-bit container.
#ifndef SPH0645_RAW_SHIFT
#define SPH0645_RAW_SHIFT 8
#endif

// === FFT / beat parameters ===
#ifndef AUDIO_SAMPLE_RATE_HZ
#define AUDIO_SAMPLE_RATE_HZ 32000
#endif

#ifndef AUDIO_FFT_SAMPLES
#define AUDIO_FFT_SAMPLES 512
#endif

#if AUDIO_SAMPLE_RATE_HZ == 48000
static constexpr uint32_t kPrimarySampleRateHz = 48000;
static constexpr uint32_t kFallbackSampleRateHz = 32000;
#elif AUDIO_SAMPLE_RATE_HZ == 32000
static constexpr uint32_t kPrimarySampleRateHz = 32000;
static constexpr uint32_t kFallbackSampleRateHz = 48000;
#else
static constexpr uint32_t kPrimarySampleRateHz = AUDIO_SAMPLE_RATE_HZ;
static constexpr uint32_t kFallbackSampleRateHz = 32000;
#endif

static constexpr uint16_t kFftSamples   = AUDIO_FFT_SAMPLES;    // power of 2
static uint32_t s_sampleRateHz = kPrimarySampleRateHz;

static constexpr float kBassMinHz = 40.0f;
static constexpr float kBassMaxHz = 180.0f;

static BeatDetectorConfig s_beatConfig = {
  0.10f,  // energyEmaAlpha
  0.20f,  // fluxEmaAlpha
  1.7f,   // fluxThreshold
  0.12f,  // fluxRiseFactor
  430,    // minBeatIntervalMs (max ~140 BPM)
  430,    // avgBeatMinMs
  800     // avgBeatMaxMs (min ~75 BPM)
};
// Beat interval averaging (tempo estimate)
static constexpr float kBeatIntervalEmaAlpha = 0.15f;  // 0.05..0.25

// Baseline pulse multiplier (the main loop further decays + clamps it).
float brightnessPulse = 1.0f;

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

#if HAS_ESP_I2S || HAS_ARDUINO_I2S
static const char* kI2SApiName =
#if HAS_ESP_I2S
  "ESP_I2S";
#elif HAS_ARDUINO_I2S
  "I2S.h";
#else
  "none";
#endif
// Beat event (edge triggered)
static volatile bool  s_beatPending  = false;
static volatile float s_beatStrength = 0.0f;

bool consumeBeat(float* strength) {
  if (!s_beatPending) return false;
  s_beatPending = false;
  if (strength) *strength = s_beatStrength;
  return true;
}

// Tempo estimate (average beat interval, ms).
// Starts at ~120 BPM (500ms).
static float s_avgBeatIntervalMs = 500.0f;

float getAverageBeatIntervalMs() {
  return s_avgBeatIntervalMs;
}

float getAverageBpm() {
  const float ms = s_avgBeatIntervalMs;
  return (ms > 1.0f) ? (60000.0f / ms) : 0.0f;
}

void getBeatDetectorConfig(BeatDetectorConfig* out) {
  if (!out) return;
  *out = s_beatConfig;
}

void setBeatDetectorConfig(const BeatDetectorConfig* cfg) {
  if (!cfg) return;
  s_beatConfig = *cfg;
}

#if HAS_ESP_I2S
static I2SClass I2S;
#endif
static bool s_i2sOk = false;
static size_t s_i2sBytesFilled = 0;

// Interleaved stereo (L,R) 32-bit words
static int32_t s_i2sRaw[kFftSamples * 2];

// Interleaved complex FFT buffer: Re[0], Im[0], Re[1], Im[1], ...
static float s_fftBuffer[kFftSamples * 2];
static float s_window[kFftSamples];
static bool s_dspReady = false;

static float s_bassEma = 0.0f;
static float s_fluxEma = 0.0f;
static float s_prevFlux = 0.0f;
static float s_prevMag[kFftSamples / 2] = {};
static uint32_t s_lastBeatMs = 0;
static uint32_t s_lastBeatIntervalMs = 0;
static uint16_t s_intervalBuffer[6] = {};
static uint8_t s_intervalCount = 0;
static uint8_t s_intervalIndex = 0;
static AudioTelemetry s_audioTelemetry = {};

#if BASS_ENVELOPE_ENABLE
static BassEnvelopeDetector s_bassEnv;
#endif

static void initDsp() {
  if (s_dspReady) return;
  if (dsps_fft2r_init_fc32(NULL, kFftSamples) != ESP_OK) {
    s_dspReady = false;
    return;
  }
  dsps_fft2r_rev_tables_init_fc32();
  dsps_wind_hann_f32(s_window, kFftSamples);
  s_dspReady = true;
}

static void initTelemetryConstants() {
  s_audioTelemetry.sampleRateHz = s_sampleRateHz;
  s_audioTelemetry.fftSamples = kFftSamples;
  s_audioTelemetry.bassMinHz = kBassMinHz;
  s_audioTelemetry.bassMaxHz = kBassMaxHz;
  s_audioTelemetry.binWidthHz = (float)s_sampleRateHz / (float)kFftSamples;
}

static bool beginI2S(uint32_t sampleRateHz) {
#if HAS_ESP_I2S
  // STD mode uses separate DIN/DOUT pins; we only need DIN for a microphone.
  // setPins(bclk, ws, dout, din, mclk)
  I2S.setPins(I2S_BCLK_PIN, I2S_WS_PIN, -1, I2S_DIN_PIN, I2S_MCLK_PIN);
  return I2S.begin(I2S_MODE_STD, sampleRateHz, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
#elif HAS_ARDUINO_I2S
  // Configure pins before begin() so the driver installs with the right mapping.
  // Order: (SCK, WS, SD, SD_OUT, SD_IN)
  I2S.setAllPins(I2S_BCLK_PIN, I2S_WS_PIN, I2S_DIN_PIN, -1, I2S_DIN_PIN);
  if (!I2S.begin(I2S_PHILIPS_MODE, (int)sampleRateHz, 32)) {
    return false;
  }
  return true;
#else
  (void)sampleRateHz;
  return false;
#endif
}

static size_t readI2SBytes(void* buffer, size_t bytes) {
#if HAS_ESP_I2S
  return I2S.readBytes((char*)buffer, bytes);
#elif HAS_ARDUINO_I2S
  const int got = I2S.read(buffer, bytes);
  return (got > 0) ? (size_t)got : 0;
#else
  (void)buffer;
  (void)bytes;
  return 0;
#endif
}

static void updateBeatIntervalAverage(uint32_t nowMs) {
  if (s_lastBeatMs == 0) return;
  uint32_t intervalMs = nowMs - s_lastBeatMs;
  if (intervalMs < s_beatConfig.avgBeatMinMs) intervalMs = s_beatConfig.avgBeatMinMs;
  if (intervalMs > s_beatConfig.avgBeatMaxMs) intervalMs = s_beatConfig.avgBeatMaxMs;

  // Keep a small rolling buffer for median-based tempo estimate.
  s_intervalBuffer[s_intervalIndex] = (uint16_t)intervalMs;
  s_intervalIndex = (uint8_t)((s_intervalIndex + 1) % (uint8_t)(sizeof(s_intervalBuffer) / sizeof(s_intervalBuffer[0])));
  if (s_intervalCount < (uint8_t)(sizeof(s_intervalBuffer) / sizeof(s_intervalBuffer[0]))) {
    s_intervalCount++;
  }

  // Median of recent intervals (robust against outliers).
  uint16_t tmp[6] = {};
  for (uint8_t i = 0; i < s_intervalCount; i++) tmp[i] = s_intervalBuffer[i];
  // Simple insertion sort (small N).
  for (uint8_t i = 1; i < s_intervalCount; i++) {
    uint16_t key = tmp[i];
    int j = (int)i - 1;
    while (j >= 0 && tmp[j] > key) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = key;
  }
  const uint16_t median = tmp[s_intervalCount / 2];

  // Exponential moving average of the median interval.
  s_avgBeatIntervalMs = (1.0f - kBeatIntervalEmaAlpha) * s_avgBeatIntervalMs +
                        (kBeatIntervalEmaAlpha * (float)median);
}

static void fakeAudioPulse() {
  static unsigned long lastKickMs = 0;
  const unsigned long now = millis();

  if (now - lastKickMs > 120UL && (uint8_t)random(0, 100) < 6) {
    // Update average beat interval for the fake beat source.
    updateBeatIntervalAverage((uint32_t)now);
    s_lastBeatMs = (uint32_t)now;

    brightnessPulse = 1.6f;
    s_beatPending = true;
    s_beatStrength = 0.7f;
    lastKickMs = now;
  }

  if (brightnessPulse < 1.0f) brightnessPulse = 1.0f;
}

void setupI2S() {
#if AUDIO_ENABLE_I2S
  s_sampleRateHz = kPrimarySampleRateHz;
  bool usedFallback = false;
  // Use 32-bit stereo so BCLK = 64 * Fs (required by SPH0645).
  s_i2sOk = beginI2S(s_sampleRateHz);
  if (!s_i2sOk && kFallbackSampleRateHz != kPrimarySampleRateHz) {
    s_sampleRateHz = kFallbackSampleRateHz;
    s_i2sOk = beginI2S(s_sampleRateHz);
    usedFallback = true;
  }
  initTelemetryConstants();
  initDsp();

  Serial.printf("I2S init (%s): sr=%lu pins BCLK=%d WS=%d DIN=%d %s%s\n",
                kI2SApiName,
                (unsigned long)s_sampleRateHz,
                I2S_BCLK_PIN,
                I2S_WS_PIN,
                I2S_DIN_PIN,
                s_i2sOk ? "OK" : "FAIL",
                usedFallback ? " (fallback)" : "");

#if BASS_ENVELOPE_ENABLE
  {
    BassEnvelopeConfig cfg = s_bassEnv.getConfig();
    cfg.sample_rate_hz = s_sampleRateHz;
    s_bassEnv.setConfig(cfg);
  }
#endif

  if (!s_i2sOk) {
    // Fall back to fake pulses so the project still runs.
    Serial.println("I2S init failed -> using fake audio pulse");
  }
#else
  s_i2sOk = false;
  initTelemetryConstants();
#endif
}

void processAudio() {
#if AUDIO_ENABLE_I2S
  if (!s_i2sOk) {
    fakeAudioPulse();
    s_audioTelemetry.i2sOk = false;
    s_audioTelemetry.bass = 0.0f;
    s_audioTelemetry.bassEma = s_bassEma;
    s_audioTelemetry.ratio = 0.0f;
    s_audioTelemetry.rise = 0.0f;
    s_audioTelemetry.threshold = 0.0f;
    s_audioTelemetry.riseThreshold = 0.0f;
    s_audioTelemetry.micRms = 0.0f;
    s_audioTelemetry.micPeak = 0.0f;
    s_audioTelemetry.intervalOk = false;
    s_audioTelemetry.above = false;
    s_audioTelemetry.rising = false;
    s_audioTelemetry.binMin = 0;
    s_audioTelemetry.binMax = 0;
    s_audioTelemetry.lastBeatMs = s_lastBeatMs;
    s_audioTelemetry.lastBeatIntervalMs = s_lastBeatIntervalMs;
    s_audioTelemetry.beatStrength = 0.0f;
    return;
  }

  s_audioTelemetry.i2sOk = true;
  const size_t wantBytes = sizeof(s_i2sRaw);
  const size_t space = wantBytes - s_i2sBytesFilled;
  const size_t gotBytes = readI2SBytes(((uint8_t*)s_i2sRaw) + s_i2sBytesFilled, space);
  if (gotBytes == 0) {
    // No data yet.
    return;
  }
  s_i2sBytesFilled += gotBytes;
  if (s_i2sBytesFilled < wantBytes) {
    // Not enough data yet.
    return;
  }
  s_i2sBytesFilled = 0;

#if BASS_ENVELOPE_ENABLE && BASS_ENVELOPE_TIME_DOMAIN
  {
    static int32_t mono[kFftSamples] = {};
    for (uint16_t i = 0; i < kFftSamples; i++) {
      const int32_t w = s_i2sRaw[(i * 2) + (SPH0645_CHANNEL ? 1 : 0)];
      const int32_t s = (SPH0645_RAW_SHIFT > 0) ? (w >> SPH0645_RAW_SHIFT) : w;
      mono[i] = s;
    }
    BassEnvelopeEvent ev{};
    if (s_bassEnv.processSamples(mono, kFftSamples, millis(), &ev)) {
      Serial.printf("BassEnv: attack=%ums sustain_release=%ums\n",
                    (unsigned)ev.attack_ms,
                    (unsigned)ev.sustain_release_ms);
    }
  }
#endif

  if (!s_dspReady) {
    initDsp();
    if (!s_dspReady) {
      s_audioTelemetry.i2sOk = false;
      return;
    }
  }

  // Convert raw I2S words into a mono buffer (choose left or right slot).
  float mean = 0.0f;
  for (uint16_t i = 0; i < kFftSamples; i++) {
    const int32_t w = s_i2sRaw[(i * 2) + (SPH0645_CHANNEL ? 1 : 0)];
    const int32_t s = (SPH0645_RAW_SHIFT > 0) ? (w >> SPH0645_RAW_SHIFT) : w;
    const float sample = (float)s;
    s_fftBuffer[(i * 2) + 0] = sample;
    s_fftBuffer[(i * 2) + 1] = 0.0f;
    mean += sample;
  }

  // DC removal + windowing (also compute raw mic level).
  mean /= (float)kFftSamples;
  float micPeak = 0.0f;
  double micSumSq = 0.0;
  for (uint16_t i = 0; i < kFftSamples; i++) {
    const float centered = s_fftBuffer[(i * 2) + 0] - mean;
    const float absVal = fabsf(centered);
    if (absVal > micPeak) micPeak = absVal;
    micSumSq += (double)centered * (double)centered;
    s_fftBuffer[(i * 2) + 0] = centered * s_window[i];
  }
  const float micRms = sqrtf((float)(micSumSq / (double)kFftSamples));

  // FFT
  dsps_fft2r_fc32(s_fftBuffer, kFftSamples);
  dsps_bit_rev_fc32(s_fftBuffer, kFftSamples);

  // Bass energy + spectral flux from magnitude bins
  const uint16_t maxBin = (kFftSamples >> 1) - 1;
  uint16_t binMin = (uint16_t)((kBassMinHz * (float)kFftSamples) / (float)s_sampleRateHz);
  uint16_t binMax = (uint16_t)((kBassMaxHz * (float)kFftSamples) / (float)s_sampleRateHz);
  if (binMin < 1) binMin = 1;
  if (binMax > maxBin) binMax = maxBin;

  float bass = 0.0f;
  float flux = 0.0f;
  for (uint16_t b = binMin; b <= binMax; b++) {
    const float re = s_fftBuffer[(b * 2) + 0];
    const float im = s_fftBuffer[(b * 2) + 1];
    const float m = sqrtf((re * re) + (im * im));
    bass += m;
    const float diff = m - s_prevMag[b];
    if (diff > 0.0f) flux += diff;
    s_prevMag[b] = m;
  }

  // Smooth baselines
  if (s_bassEma <= 0.0001f) s_bassEma = bass;
  s_bassEma = (1.0f - s_beatConfig.energyEmaAlpha) * s_bassEma + s_beatConfig.energyEmaAlpha * bass;
  if (s_fluxEma <= 0.0001f) s_fluxEma = flux;
  s_fluxEma = (1.0f - s_beatConfig.fluxEmaAlpha) * s_fluxEma + s_beatConfig.fluxEmaAlpha * flux;

  // Beat decision
  const uint32_t now = millis();
  const uint32_t intervalMs = (s_lastBeatMs > 0) ? (now - s_lastBeatMs) : 0;
  const bool intervalOk = (now - s_lastBeatMs) >= s_beatConfig.minBeatIntervalMs;
  const float rise = flux - s_prevFlux;
  const bool above = flux > (s_fluxEma * s_beatConfig.fluxThreshold);
  const bool rising = rise > (s_fluxEma * s_beatConfig.fluxRiseFactor);

  const float ratio = flux / (s_fluxEma + 1e-3f);
  s_audioTelemetry.bass = bass;
  s_audioTelemetry.bassEma = s_bassEma;
  s_audioTelemetry.ratio = ratio;
  s_audioTelemetry.rise = rise;
  s_audioTelemetry.threshold = s_fluxEma * s_beatConfig.fluxThreshold;
  s_audioTelemetry.riseThreshold = s_fluxEma * s_beatConfig.fluxRiseFactor;
  s_audioTelemetry.micRms = micRms;
  s_audioTelemetry.micPeak = micPeak;
  s_audioTelemetry.intervalOk = intervalOk;
  s_audioTelemetry.above = above;
  s_audioTelemetry.rising = rising;
  s_audioTelemetry.binMin = binMin;
  s_audioTelemetry.binMax = binMax;
  s_audioTelemetry.lastBeatMs = s_lastBeatMs;
  s_audioTelemetry.lastBeatIntervalMs = s_lastBeatIntervalMs;
  s_audioTelemetry.beatStrength = 0.0f;

#if BASS_ENVELOPE_ENABLE
  {
    BassEnvelopeEvent ev{};
    if (s_bassEnv.processEnvelope(bass, now, &ev)) {
      Serial.printf("BassEnv(FFT): attack=%ums sustain_release=%ums\n",
                    (unsigned)ev.attack_ms,
                    (unsigned)ev.sustain_release_ms);
    }
  }
#endif

  if (intervalOk && above && rising) {
    const float strength = clamp01((ratio - s_beatConfig.fluxThreshold) / s_beatConfig.fluxThreshold);

    s_beatPending = true;
    s_beatStrength = strength;
    s_audioTelemetry.beatStrength = strength;

    // Update average beat interval (tempo estimate) before resetting the timer.
    updateBeatIntervalAverage(now);
    s_lastBeatIntervalMs = intervalMs;
    s_lastBeatMs = now;
    s_audioTelemetry.lastBeatMs = s_lastBeatMs;
    s_audioTelemetry.lastBeatIntervalMs = s_lastBeatIntervalMs;

    // Also drive the global brightness pulse.
    const float pulse = 1.0f + (0.9f * strength);
    if (brightnessPulse < pulse) brightnessPulse = pulse;
  }

  s_prevFlux = flux;

  if (brightnessPulse < 1.0f) brightnessPulse = 1.0f;
#else
  fakeAudioPulse();
#endif
}
#else
// Fallback stubs for environments without I2S support.
static float s_avgBeatIntervalMs = 500.0f;
static AudioTelemetry s_audioTelemetry = {};
bool consumeBeat(float* strength) {
  if (strength) *strength = 0.0f;
  return false;
}

float getAverageBeatIntervalMs() {
  return s_avgBeatIntervalMs;
}

float getAverageBpm() {
  const float ms = s_avgBeatIntervalMs;
  return (ms > 1.0f) ? (60000.0f / ms) : 0.0f;
}

void setupI2S() {
  s_sampleRateHz = kPrimarySampleRateHz;
  s_audioTelemetry.sampleRateHz = s_sampleRateHz;
  s_audioTelemetry.fftSamples = kFftSamples;
  s_audioTelemetry.bassMinHz = kBassMinHz;
  s_audioTelemetry.bassMaxHz = kBassMaxHz;
  s_audioTelemetry.binWidthHz = (float)s_sampleRateHz / (float)kFftSamples;
  s_audioTelemetry.micRms = 0.0f;
  s_audioTelemetry.micPeak = 0.0f;
  s_audioTelemetry.i2sOk = false;
  Serial.println("ESP_I2S.h not available; audio disabled");
}

void processAudio() {
  // No-op without ESP_I2S support.
}
#endif

void getAudioTelemetry(AudioTelemetry* out) {
  if (!out) return;
  *out = s_audioTelemetry;
}
