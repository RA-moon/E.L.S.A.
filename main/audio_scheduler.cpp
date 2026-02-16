#include "audio_scheduler.h"

#include "audio_processor.h"
#include "elsa_config.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static uint32_t s_lastAudioTime = 0;
static const char* TAG = "audio_sched";

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
    processAudio();
    vTaskDelayUntil(&lastWake, delayTicks);
  }
}
#endif

void audioSchedulerInit() {
  s_lastAudioTime = millis_idf();
#if AUDIO_TASK_ENABLE
  if (xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 5, nullptr, 0) != pdPASS) {
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
