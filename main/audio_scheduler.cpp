#include "audio_scheduler.h"

#include "audio_processor.h"
#include "elsa_config.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static uint32_t s_lastAudioTime = 0;
static const char* TAG = "audio_sched";

#if PROFILE_PERF
static uint64_t s_audioBusyUs = 0;
static uint32_t s_audioCalls = 0;
static uint64_t s_audioWindowStartUs = 0;
#endif

static inline uint32_t millis_idf() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline uint32_t micros_idf() {
  return (uint32_t)esp_timer_get_time();
}

#if AUDIO_TASK_ENABLE
static void audioTask(void* param) {
  (void)param;
  const TickType_t delayTicks = pdMS_TO_TICKS(AUDIO_INTERVAL);
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
#if PROFILE_PERF
    const uint64_t t0 = esp_timer_get_time();
#endif
    processAudio();
#if PROFILE_PERF
    const uint64_t t1 = esp_timer_get_time();
    if (s_audioWindowStartUs == 0) s_audioWindowStartUs = t0;
    s_audioBusyUs += (t1 - t0);
    s_audioCalls++;
    const uint64_t windowUs = t1 - s_audioWindowStartUs;
    if (windowUs >= (uint64_t)PROFILE_INTERVAL_MS * 1000ULL) {
      const float duty = (windowUs > 0) ? (100.0f * (float)s_audioBusyUs / (float)windowUs) : 0.0f;
      const float avgUs = (s_audioCalls > 0) ? ((float)s_audioBusyUs / (float)s_audioCalls) : 0.0f;
      ESP_LOGI(TAG, "audio duty=%.1f%% avg=%.1fus calls=%u", duty, avgUs, s_audioCalls);
      s_audioBusyUs = 0;
      s_audioCalls = 0;
      s_audioWindowStartUs = t1;
    }
#endif
    vTaskDelayUntil(&lastWake, delayTicks);
  }
}
#endif

void audioSchedulerInit() {
  s_lastAudioTime = millis_idf();
#if AUDIO_TASK_ENABLE
  if (xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, AUDIO_TASK_PRIORITY, nullptr, AUDIO_TASK_CORE) != pdPASS) {
    ESP_LOGW(TAG, "Audio task create failed");
  }
#endif
}

bool audioSchedulerRun(uint32_t nowMs, uint32_t* outDurationUs) {
#if AUDIO_TASK_ENABLE
  (void)nowMs;
  if (outDurationUs) *outDurationUs = 0;
  return false;
#else
  if ((nowMs - s_lastAudioTime) >= AUDIO_INTERVAL) {
    const uint32_t t0 = micros_idf();
    processAudio();
    const uint32_t dt = (uint32_t)(micros_idf() - t0);
    s_lastAudioTime = nowMs;
    if (outDurationUs) *outDurationUs = dt;
    return true;
  }
  if (outDurationUs) *outDurationUs = 0;
  return false;
#endif
}
