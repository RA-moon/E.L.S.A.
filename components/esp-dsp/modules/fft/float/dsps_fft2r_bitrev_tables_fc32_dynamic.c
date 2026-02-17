// Copyright 2018-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dsps_fft_tables.h"
#include "sdkconfig.h"

#ifndef CONFIG_DSP_MAX_FFT_SIZE
#define CONFIG_DSP_MAX_FFT_SIZE 4096
#endif

#if CONFIG_DSP_MAX_FFT_SIZE >= 16
static uint16_t s_table16[2 * 6];
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 32
static uint16_t s_table32[2 * 12];
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 64
static uint16_t s_table64[2 * 28];
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 128
static uint16_t s_table128[2 * 56];
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 256
static uint16_t s_table256[2 * 120];
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 512
static uint16_t s_table512[2 * 240];
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 1024
static uint16_t s_table1024[2 * 496];
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 2048
static uint16_t s_table2048[2 * 992];
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 4096
static uint16_t s_table4096[2 * 2016];
#endif

static bool s_tablesGenerated = false;

static void generate_bitrev_table(uint16_t* out, int n) {
  int j = 0;
  int outPos = 0;
  for (int i = 1; i < (n - 1); i++) {
    int k = n >> 1;
    while (k <= j) {
      j -= k;
      k >>= 1;
    }
    j += k;
    if (i < j) {
      // esp-dsp table format is byte offsets in complex-float interleaved array.
      out[outPos++] = (uint16_t)(i * 8);
      out[outPos++] = (uint16_t)(j * 8);
    }
  }
}

void dsps_fft2r_rev_tables_init_fc32(void) {
  if (!s_tablesGenerated) {
#if CONFIG_DSP_MAX_FFT_SIZE >= 16
    generate_bitrev_table(s_table16, 16);
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 32
    generate_bitrev_table(s_table32, 32);
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 64
    generate_bitrev_table(s_table64, 64);
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 128
    generate_bitrev_table(s_table128, 128);
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 256
    generate_bitrev_table(s_table256, 256);
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 512
    generate_bitrev_table(s_table512, 512);
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 1024
    generate_bitrev_table(s_table1024, 1024);
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 2048
    generate_bitrev_table(s_table2048, 2048);
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 4096
    generate_bitrev_table(s_table4096, 4096);
#endif
    s_tablesGenerated = true;
  }

#if CONFIG_DSP_MAX_FFT_SIZE >= 16
  dsps_fft2r_rev_tables_fc32[0] = s_table16;
#else
  dsps_fft2r_rev_tables_fc32[0] = NULL;
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 32
  dsps_fft2r_rev_tables_fc32[1] = s_table32;
#else
  dsps_fft2r_rev_tables_fc32[1] = NULL;
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 64
  dsps_fft2r_rev_tables_fc32[2] = s_table64;
#else
  dsps_fft2r_rev_tables_fc32[2] = NULL;
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 128
  dsps_fft2r_rev_tables_fc32[3] = s_table128;
#else
  dsps_fft2r_rev_tables_fc32[3] = NULL;
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 256
  dsps_fft2r_rev_tables_fc32[4] = s_table256;
#else
  dsps_fft2r_rev_tables_fc32[4] = NULL;
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 512
  dsps_fft2r_rev_tables_fc32[5] = s_table512;
#else
  dsps_fft2r_rev_tables_fc32[5] = NULL;
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 1024
  dsps_fft2r_rev_tables_fc32[6] = s_table1024;
#else
  dsps_fft2r_rev_tables_fc32[6] = NULL;
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 2048
  dsps_fft2r_rev_tables_fc32[7] = s_table2048;
#else
  dsps_fft2r_rev_tables_fc32[7] = NULL;
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 4096
  dsps_fft2r_rev_tables_fc32[8] = s_table4096;
#else
  dsps_fft2r_rev_tables_fc32[8] = NULL;
#endif
}

uint16_t* dsps_fft2r_rev_tables_fc32[] = {
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

const uint16_t dsps_fft2r_rev_tables_fc32_size[] = {
  6,
  12,
  28,
  56,
  120,
#if CONFIG_DSP_MAX_FFT_SIZE >= 512
  240,
#else
  0,
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 1024
  496,
#else
  0,
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 2048
  992,
#else
  0,
#endif
#if CONFIG_DSP_MAX_FFT_SIZE >= 4096
  2016,
#else
  0,
#endif
};

// Keep extern symbols declared in dsps_fft_tables.h available.
const uint16_t bitrev2r_table_16_fc32[] = {0};
const uint16_t bitrev2r_table_32_fc32[] = {0};
const uint16_t bitrev2r_table_64_fc32[] = {0};
const uint16_t bitrev2r_table_128_fc32[] = {0};
const uint16_t bitrev2r_table_256_fc32[] = {0};
const uint16_t bitrev2r_table_512_fc32[] = {0};
const uint16_t bitrev2r_table_1024_fc32[] = {0};
const uint16_t bitrev2r_table_2048_fc32[] = {0};
const uint16_t bitrev2r_table_4096_fc32[] = {0};

const uint16_t bitrev2r_table_16_fc32_size = 0;
const uint16_t bitrev2r_table_32_fc32_size = 0;
const uint16_t bitrev2r_table_64_fc32_size = 0;
const uint16_t bitrev2r_table_128_fc32_size = 0;
const uint16_t bitrev2r_table_256_fc32_size = 0;
const uint16_t bitrev2r_table_512_fc32_size = 0;
const uint16_t bitrev2r_table_1024_fc32_size = 0;
const uint16_t bitrev2r_table_2048_fc32_size = 0;
const uint16_t bitrev2r_table_4096_fc32_size = 0;
