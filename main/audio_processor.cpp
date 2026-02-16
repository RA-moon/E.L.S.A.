#include "audio_processor.h"

#include <math.h>

#include "driver/i2s_std.h"
#include "dsps_fft2r.h"
#include "dsps_wind_hann.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

// === I2S pin mapping (adjust to your wiring) ===
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
#ifndef SPH0645_CHANNEL
#define SPH0645_CHANNEL 0
#endif
#ifndef SPH0645_RAW_SHIFT
#define SPH0645_RAW_SHIFT 8
#endif

// === FFT / beat parameters ===
#ifndef AUDIO_SAMPLE_RATE_HZ
#define AUDIO_SAMPLE_RATE_HZ 32000
#endif
#ifndef AUDIO_FFT_SAMPLES
#define AUDIO_FFT_SAMPLES 256
#endif

static constexpr float kBassMinHz = 40.0f;
static constexpr float kBassMaxHz = 180.0f;
static constexpr float kBeatIntervalEmaAlpha = 0.15f;

static BeatDetectorConfig s_beatConfig = {
  0.10f,
  0.20f,
  1.7f,
  0.12f,
  430,
  430,
  800
};

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static const char* TAG = "audio";

static i2s_chan_handle_t s_i2s_rx = NULL;
static bool s_i2sOk = false;

static DRAM_ATTR alignas(16) float s_window[AUDIO_FFT_SAMPLES];
static DRAM_ATTR alignas(16) float s_fftBuffer[AUDIO_FFT_SAMPLES * 2];
static DRAM_ATTR alignas(16) float s_prevMag[AUDIO_FFT_SAMPLES / 2] = {};
static bool s_dspReady = false;

static volatile bool s_beatPending = false;
static volatile float s_beatStrength = 0.0f;

static float s_avgBeatIntervalMs = 500.0f;
static uint32_t s_lastBeatMs = 0;
static uint32_t s_lastBeatIntervalMs = 0;
static uint32_t s_lastRealBeatMs = 0;
static float s_bassEma = 0.0f;
static float s_fluxEma = 0.0f;
static float s_prevFlux = 0.0f;
static uint16_t s_intervalBuffer[6] = {};
static uint8_t s_intervalCount = 0;
static uint8_t s_intervalIndex = 0;
static AudioTelemetry s_audioTelemetry = {};

static inline uint32_t millis_idf() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void initDsp() {
  if (s_dspReady) return;
  if (dsps_fft2r_init_fc32(NULL, AUDIO_FFT_SAMPLES) != ESP_OK) {
    s_dspReady = false;
    return;
  }
  dsps_fft2r_rev_tables_init_fc32();
  dsps_wind_hann_f32(s_window, AUDIO_FFT_SAMPLES);
  s_dspReady = true;
}

bool consumeBeat(float* strength) {
  if (!s_beatPending) return false;
  s_beatPending = false;
  if (strength) *strength = s_beatStrength;
  return true;
}

float getAverageBeatIntervalMs() {
  return s_avgBeatIntervalMs;
}

uint32_t getLastRealBeatMs() {
  return s_lastRealBeatMs;
}

float getAverageBpm() {
  const float ms = s_avgBeatIntervalMs;
  return (ms > 1.0f) ? (60000.0f / ms) : 0.0f;
}

void getAudioTelemetry(AudioTelemetry* out) {
  if (!out) return;
  *out = s_audioTelemetry;
}

void getBeatDetectorConfig(BeatDetectorConfig* out) {
  if (!out) return;
  *out = s_beatConfig;
}

void setBeatDetectorConfig(const BeatDetectorConfig* cfg) {
  if (!cfg) return;
  s_beatConfig = *cfg;
}

static void updateBeatIntervalAverage(uint32_t nowMs) {
  if (s_lastBeatMs == 0) return;
  uint32_t intervalMs = nowMs - s_lastBeatMs;
  if (intervalMs < s_beatConfig.avgBeatMinMs) intervalMs = s_beatConfig.avgBeatMinMs;
  if (intervalMs > s_beatConfig.avgBeatMaxMs) intervalMs = s_beatConfig.avgBeatMaxMs;

  s_intervalBuffer[s_intervalIndex] = (uint16_t)intervalMs;
  s_intervalIndex = (uint8_t)((s_intervalIndex + 1) % (uint8_t)(sizeof(s_intervalBuffer) / sizeof(s_intervalBuffer[0])));
  if (s_intervalCount < (uint8_t)(sizeof(s_intervalBuffer) / sizeof(s_intervalBuffer[0]))) {
    s_intervalCount++;
  }

  uint16_t tmp[6] = {};
  for (uint8_t i = 0; i < s_intervalCount; i++) tmp[i] = s_intervalBuffer[i];
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

  s_avgBeatIntervalMs = (1.0f - kBeatIntervalEmaAlpha) * s_avgBeatIntervalMs +
                        (kBeatIntervalEmaAlpha * (float)median);
}

static void fakeAudioPulse() {
  static uint32_t lastKickMs = 0;
  const uint32_t now = millis_idf();

  if (now - lastKickMs > 120U && (esp_random() % 100) < 6) {
    updateBeatIntervalAverage(now);
    s_lastBeatMs = now;
    s_beatPending = true;
    s_beatStrength = 0.7f;
    lastKickMs = now;
  }
}

void setupI2S() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  if (i2s_new_channel(&chan_cfg, NULL, &s_i2s_rx) != ESP_OK) {
    ESP_LOGE(TAG, "I2S channel init failed");
    s_i2sOk = false;
  } else {
    i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
        .mclk = I2S_MCLK_PIN < 0 ? I2S_GPIO_UNUSED : (gpio_num_t)I2S_MCLK_PIN,
        .bclk = (gpio_num_t)I2S_BCLK_PIN,
        .ws = (gpio_num_t)I2S_WS_PIN,
        .dout = I2S_GPIO_UNUSED,
        .din = (gpio_num_t)I2S_DIN_PIN,
        .invert_flags = {
          .mclk_inv = false,
          .bclk_inv = false,
          .ws_inv = false,
        },
      },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    if (i2s_channel_init_std_mode(s_i2s_rx, &std_cfg) != ESP_OK ||
        i2s_channel_enable(s_i2s_rx) != ESP_OK) {
      ESP_LOGE(TAG, "I2S start failed");
      s_i2sOk = false;
    } else {
      s_i2sOk = true;
    }
  }

  initDsp();

  s_audioTelemetry.sampleRateHz = AUDIO_SAMPLE_RATE_HZ;
  s_audioTelemetry.fftSamples = AUDIO_FFT_SAMPLES;
  s_audioTelemetry.bassMinHz = kBassMinHz;
  s_audioTelemetry.bassMaxHz = kBassMaxHz;
  s_audioTelemetry.binWidthHz = (float)AUDIO_SAMPLE_RATE_HZ / (float)AUDIO_FFT_SAMPLES;
  s_audioTelemetry.i2sOk = s_i2sOk;

  ESP_LOGI(TAG, "I2S init: sr=%u pins BCLK=%d WS=%d DIN=%d %s",
           AUDIO_SAMPLE_RATE_HZ,
           I2S_BCLK_PIN,
           I2S_WS_PIN,
           I2S_DIN_PIN,
           s_i2sOk ? "OK" : "FAIL");
}

void processAudio() {
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

  int32_t raw[AUDIO_FFT_SAMPLES] = {};
  size_t bytes_read = 0;
  if (i2s_channel_read(s_i2s_rx, raw, sizeof(raw), &bytes_read, portMAX_DELAY) != ESP_OK ||
      bytes_read < sizeof(raw)) {
    return;
  }

  float mean = 0.0f;
  for (int i = 0; i < AUDIO_FFT_SAMPLES; i++) {
    const int32_t w = raw[i];
    const int32_t s = (SPH0645_RAW_SHIFT > 0) ? (w >> SPH0645_RAW_SHIFT) : w;
    mean += (float)s;
  }
  mean /= (float)AUDIO_FFT_SAMPLES;

  if (!s_dspReady) {
    initDsp();
    if (!s_dspReady) {
      s_audioTelemetry.i2sOk = false;
      return;
    }
  }

  double micSumSq = 0.0;
  float micPeak = 0.0f;
  for (int i = 0; i < AUDIO_FFT_SAMPLES; i++) {
    const int32_t w = raw[i];
    const float s = (float)((SPH0645_RAW_SHIFT > 0) ? (w >> SPH0645_RAW_SHIFT) : w);
    const float centered = s - mean;
    const float absVal = fabsf(centered);
    if (absVal > micPeak) micPeak = absVal;
    micSumSq += (double)centered * (double)centered;

    s_fftBuffer[(i * 2) + 0] = centered * s_window[i];
    s_fftBuffer[(i * 2) + 1] = 0.0f;
  }
  const float micRms = sqrtf((float)(micSumSq / (double)AUDIO_FFT_SAMPLES));

  const float bin_width = (float)AUDIO_SAMPLE_RATE_HZ / (float)AUDIO_FFT_SAMPLES;
  int bin_min = (int)floorf(kBassMinHz / bin_width);
  int bin_max = (int)floorf(kBassMaxHz / bin_width);
  if (bin_min < 1) bin_min = 1;
  if (bin_max > (AUDIO_FFT_SAMPLES / 2 - 1)) bin_max = (AUDIO_FFT_SAMPLES / 2 - 1);

  float bass = 0.0f;
  float flux = 0.0f;
  dsps_fft2r_fc32(s_fftBuffer, AUDIO_FFT_SAMPLES);
  dsps_bit_rev_fc32(s_fftBuffer, AUDIO_FFT_SAMPLES);

  for (int k = bin_min; k <= bin_max; k++) {
    const float re = s_fftBuffer[(k * 2) + 0];
    const float im = s_fftBuffer[(k * 2) + 1];
    const float m = sqrtf((re * re) + (im * im));
    bass += m;
    const float diff = m - s_prevMag[k];
    if (diff > 0.0f) flux += diff;
    s_prevMag[k] = m;
  }

  if (s_bassEma <= 0.0001f) s_bassEma = bass;
  s_bassEma = (1.0f - s_beatConfig.energyEmaAlpha) * s_bassEma + s_beatConfig.energyEmaAlpha * bass;
  if (s_fluxEma <= 0.0001f) s_fluxEma = flux;
  s_fluxEma = (1.0f - s_beatConfig.fluxEmaAlpha) * s_fluxEma + s_beatConfig.fluxEmaAlpha * flux;

  const uint32_t now = millis_idf();
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
  s_audioTelemetry.binMin = (uint16_t)bin_min;
  s_audioTelemetry.binMax = (uint16_t)bin_max;
  s_audioTelemetry.lastBeatMs = s_lastBeatMs;
  s_audioTelemetry.lastBeatIntervalMs = s_lastBeatIntervalMs;
  s_audioTelemetry.beatStrength = 0.0f;
  s_audioTelemetry.i2sOk = true;

  if (intervalOk && above && rising) {
    const float strength = clamp01((ratio - s_beatConfig.fluxThreshold) / s_beatConfig.fluxThreshold);
    s_beatPending = true;
    s_beatStrength = strength;
    s_audioTelemetry.beatStrength = strength;

    updateBeatIntervalAverage(now);
    s_lastBeatIntervalMs = intervalMs;
    s_lastBeatMs = now;
    s_lastRealBeatMs = now;
    s_audioTelemetry.lastBeatMs = s_lastBeatMs;
    s_audioTelemetry.lastBeatIntervalMs = s_lastBeatIntervalMs;
  }

  s_prevFlux = flux;
}
