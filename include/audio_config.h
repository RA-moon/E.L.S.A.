#pragma once

// Shared audio tuning so all translation units see the same values.
// Override via build_flags if needed.
#if defined(ARDUINO_ESP32S3_DEV) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ESP32S3)
#ifndef AUDIO_SAMPLE_RATE_HZ
#define AUDIO_SAMPLE_RATE_HZ 32000
#endif
#ifndef AUDIO_FFT_SAMPLES
#define AUDIO_FFT_SAMPLES 256
#endif
#endif
