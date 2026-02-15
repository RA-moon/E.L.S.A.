#include "audio_scheduler.h"

#include <Arduino.h>

#include "audio_processor.h"
#include "elsa_config.h"

#if AUDIO_TASK_ENABLE
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static uint32_t s_lastAudioTime = 0;

#if AUDIO_TASK_ENABLE
static void audioTask(void* param) {
  (void)param;
  const TickType_t delayTicks = pdMS_TO_TICKS(AUDIO_INTERVAL);
  for (;;) {
    processAudio();
    vTaskDelay(delayTicks);
  }
}
#endif

void audioSchedulerInit() {
  s_lastAudioTime = millis();
#if AUDIO_TASK_ENABLE
  if (xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("Audio task create failed");
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
    const uint32_t t0 = micros();
    processAudio();
    const uint32_t dt = (uint32_t)(micros() - t0);
    s_lastAudioTime = nowMs;
    if (outDurationUs) *outDurationUs = dt;
    return true;
  }
  if (outDurationUs) *outDurationUs = 0;
  return false;
#endif
}
