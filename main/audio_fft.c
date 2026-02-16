#include "audio_fft.h"

#include <math.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define I2S_BCLK_PIN 5
#define I2S_WS_PIN 6
#define I2S_DIN_PIN 7
#define I2S_SAMPLE_RATE_HZ 32000
#define I2S_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_32BIT
#define I2S_CHANNELS I2S_SLOT_MODE_STEREO
#define SPH0645_CHANNEL 0
#define SPH0645_RAW_SHIFT 8

#define FFT_SAMPLES 256
#define BASS_MIN_HZ 40.0f
#define BASS_MAX_HZ 180.0f

static const char* TAG = "audio_fft";

static i2s_chan_handle_t s_i2s_rx = NULL;
static float s_samples[FFT_SAMPLES];
static float s_window[FFT_SAMPLES];
static audio_metrics_t s_metrics = {0};
static portMUX_TYPE s_metrics_mux = portMUX_INITIALIZER_UNLOCKED;

static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void audio_task(void* arg) {
  (void)arg;
  int32_t raw[FFT_SAMPLES * 2];

  for (;;) {
    size_t bytes_read = 0;
    const esp_err_t err = i2s_channel_read(
      s_i2s_rx,
      raw,
      sizeof(raw),
      &bytes_read,
      portMAX_DELAY
    );
    if (err != ESP_OK || bytes_read < sizeof(raw)) {
      continue;
    }

    float mean = 0.0f;
    for (int i = 0; i < FFT_SAMPLES; i++) {
      const int32_t w = raw[(i * 2) + (SPH0645_CHANNEL ? 1 : 0)];
      const int32_t s = (SPH0645_RAW_SHIFT > 0) ? (w >> SPH0645_RAW_SHIFT) : w;
      mean += (float)s;
    }
    mean /= (float)FFT_SAMPLES;

    float rms_acc = 0.0f;
    for (int i = 0; i < FFT_SAMPLES; i++) {
      const int32_t w = raw[(i * 2) + (SPH0645_CHANNEL ? 1 : 0)];
      const float s = (float)((SPH0645_RAW_SHIFT > 0) ? (w >> SPH0645_RAW_SHIFT) : w);
      const float centered = s - mean;
      const float normalized = centered / 8388608.0f; // 2^23
      rms_acc += normalized * normalized;

      s_samples[i] = normalized * s_window[i];
    }

    const float rms = sqrtf(rms_acc / (float)FFT_SAMPLES);

    const float bin_width = (float)I2S_SAMPLE_RATE_HZ / (float)FFT_SAMPLES;
    int bin_min = (int)floorf(BASS_MIN_HZ / bin_width);
    int bin_max = (int)floorf(BASS_MAX_HZ / bin_width);
    if (bin_min < 1) bin_min = 1;
    if (bin_max > (FFT_SAMPLES / 2 - 1)) bin_max = (FFT_SAMPLES / 2 - 1);

    float bass = 0.0f;
    const float two_pi = 2.0f * (float)M_PI;
    for (int k = bin_min; k <= bin_max; k++) {
      float re = 0.0f;
      float im = 0.0f;
      for (int n = 0; n < FFT_SAMPLES; n++) {
        const float angle = two_pi * (float)k * (float)n / (float)FFT_SAMPLES;
        re += s_samples[n] * cosf(angle);
        im -= s_samples[n] * sinf(angle);
      }
      bass += sqrtf((re * re) + (im * im));
    }

    portENTER_CRITICAL(&s_metrics_mux);
    s_metrics.rms = rms;
    s_metrics.bass = bass;
    s_metrics.sample_rate_hz = I2S_SAMPLE_RATE_HZ;
    s_metrics.fft_size = FFT_SAMPLES;
    portEXIT_CRITICAL(&s_metrics_mux);
  }
}

void audio_fft_init(void) {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_i2s_rx));

  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE_HZ),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS_PER_SAMPLE, I2S_CHANNELS),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = I2S_BCLK_PIN,
      .ws = I2S_WS_PIN,
      .dout = I2S_GPIO_UNUSED,
      .din = I2S_DIN_PIN,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_rx, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_rx));

  for (int i = 0; i < FFT_SAMPLES; i++) {
    const float phase = (2.0f * (float)M_PI * (float)i) / (float)(FFT_SAMPLES - 1);
    s_window[i] = 0.5f * (1.0f - cosf(phase));
  }

  const BaseType_t ok = xTaskCreatePinnedToCore(audio_task, "audio_fft", 4096, NULL, 5, NULL, 0);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create audio task");
  }
}

bool audio_fft_get_metrics(audio_metrics_t* out) {
  if (!out) return false;
  portENTER_CRITICAL(&s_metrics_mux);
  *out = s_metrics;
  portEXIT_CRITICAL(&s_metrics_mux);
  return true;
}
