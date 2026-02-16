#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  float rms;
  float bass;
  uint32_t sample_rate_hz;
  uint32_t fft_size;
} audio_metrics_t;

void audio_fft_init(void);
bool audio_fft_get_metrics(audio_metrics_t* out);
