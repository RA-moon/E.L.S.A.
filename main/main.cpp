#include "animation_engine.h"
#include "audio_processor.h"
#include "audio_scheduler.h"
#include "elsa_config.h"
#include "hair_strip.h"
#include "led_strip_driver.h"
#include "led_utils.h"
#include "runtime_config.h"
#include "waveform.h"
#include "wave_position.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "elsa-idf";

static inline uint32_t now_ms() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline int64_t now_us() {
  return esp_timer_get_time();
}

#if RENDER_TARGET_FPS > 0
static void delay_until_us(int64_t target_us) {
  const int64_t remaining = target_us - now_us();
  if (remaining <= 0) return;
  const int64_t tick_us = (int64_t)portTICK_PERIOD_MS * 1000LL;
  const TickType_t ticks = (TickType_t)((remaining + tick_us - 1) / tick_us);
  if (ticks > 0) {
    vTaskDelay(ticks);
  }
}
#endif

static led_strip_device_t s_brain = {};
#if ENABLE_HAIR_STRIP
static led_strip_device_t s_hair = {};
#endif

static Rgb* s_brain_buf = nullptr;
#if ENABLE_HAIR_STRIP
static Rgb* s_hair_buf = nullptr;
#endif

#if PROFILE_PERF
static uint64_t s_renderBusyUs = 0;
static uint32_t s_renderFrames = 0;
static uint64_t s_renderWindowStartUs = 0;
#endif
#if RENDER_FPS_LOG_MS > 0
static uint32_t s_renderFpsFrames = 0;
static uint64_t s_renderFpsWindowStartUs = 0;
#endif
#if SCHED_TELEMETRY_LOG_MS > 0
static uint64_t s_schedTelemetryWindowStartUs = 0;
#endif

static void showStrips() {
  led_strip_device_show_async(&s_brain);
#if ENABLE_HAIR_STRIP
  led_strip_device_show_async(&s_hair);
#endif
}

static void render_task(void* arg) {
  (void)arg;
#if RENDER_TARGET_FPS > 0
  const int64_t framePeriodUs = 1000000LL / (int64_t)RENDER_TARGET_FPS;
  int64_t nextFrameUs = now_us();
#else
  const TickType_t delayTicks = pdMS_TO_TICKS(DELAY_MS);
  TickType_t lastWake = xTaskGetTickCount();
#endif

  for (;;) {
#if PROFILE_PERF
    const uint64_t frameStartUs = esp_timer_get_time();
#endif
    const uint32_t now = now_ms();

#if !TEST_SOLID_COLOR && !AUDIO_TASK_ENABLE
    audioSchedulerRun(now, nullptr);
#endif

    bool doRender = true;
#if SKIP_RENDER_WHEN_BUSY
    doRender = !led_strip_device_is_busy(&s_brain);
#endif
    if (doRender) {
      s_brain_buf = (Rgb*)led_strip_device_get_write_buffer(&s_brain);
      if (!s_brain_buf) {
        doRender = false;
      } else {
        animationEngineSetBuffer(s_brain_buf);
      }
    }

#if TEST_SOLID_COLOR
    if (doRender) {
      static bool on = false;
      static uint32_t lastToggle = 0;
      if (now - lastToggle >= 1000) {
        on = !on;
        lastToggle = now;
      }
      const uint16_t count = (TEST_LED_COUNT < NUM_LEDS1) ? TEST_LED_COUNT : NUM_LEDS1;
      const Rgb color = on ? Rgb{255, 255, 255} : Rgb{0, 0, 0};
      for (uint16_t i = 0; i < NUM_LEDS1; i++) {
        s_brain_buf[i] = (i < count) ? color : Rgb{0, 0, 0};
      }
#if ENABLE_HAIR_STRIP
      s_hair_buf = (Rgb*)led_strip_device_get_write_buffer(&s_hair);
      if (s_hair_buf) {
        for (uint16_t i = 0; i < NUM_LEDS2; i++) {
          s_hair_buf[i] = color;
        }
      }
#endif
      showStrips();
#if RENDER_FPS_LOG_MS > 0
      s_renderFpsFrames++;
#endif
    }
#else
    if (doRender) {
      runLedAnimation(now);
#if ENABLE_HAIR_STRIP
      s_hair_buf = (Rgb*)led_strip_device_get_write_buffer(&s_hair);
      if (s_hair_buf) {
        updateHairStrip(now, s_hair_buf, NUM_LEDS2);
      }
#endif
      showStrips();
#if RENDER_FPS_LOG_MS > 0
      s_renderFpsFrames++;
#endif
    }
#endif
#if RENDER_FPS_LOG_MS > 0
    const uint64_t nowUs = esp_timer_get_time();
    if (s_renderFpsWindowStartUs == 0) s_renderFpsWindowStartUs = nowUs;
    const uint64_t windowUs = nowUs - s_renderFpsWindowStartUs;
    if (windowUs >= (uint64_t)RENDER_FPS_LOG_MS * 1000ULL) {
      const float fps = (windowUs > 0) ? (1e6f * (float)s_renderFpsFrames / (float)windowUs) : 0.0f;
      ESP_LOGI(TAG, "render fps=%.1f", fps);
      s_renderFpsFrames = 0;
      s_renderFpsWindowStartUs = nowUs;
    }
#endif
#if SCHED_TELEMETRY_LOG_MS > 0
    const uint64_t telemetryNowUs = esp_timer_get_time();
    if (s_schedTelemetryWindowStartUs == 0) s_schedTelemetryWindowStartUs = telemetryNowUs;
    const uint64_t telemetryWindowUs = telemetryNowUs - s_schedTelemetryWindowStartUs;
    if (telemetryWindowUs >= (uint64_t)SCHED_TELEMETRY_LOG_MS * 1000ULL) {
      AnimationSchedulerTelemetry sched = {};
      getAnimationSchedulerTelemetry(&sched);
      const uint32_t sinceSpawnMs =
        (sched.lastSpawnMs > 0 && now >= sched.lastSpawnMs) ? (now - sched.lastSpawnMs) : 0;
      ESP_LOGI(TAG,
               "sched spawn=%s since=%ums fallback=%ums",
               waveSpawnReasonName(sched.lastSpawnReason),
               (unsigned)sinceSpawnMs,
               (unsigned)sched.lastFallbackIntervalMs);
      s_schedTelemetryWindowStartUs = telemetryNowUs;
    }
#endif
#if RENDER_TARGET_FPS > 0
    nextFrameUs += framePeriodUs;
    const int64_t nowFrameUs = now_us();
    if (nextFrameUs < nowFrameUs) nextFrameUs = nowFrameUs + framePeriodUs;
    delay_until_us(nextFrameUs);
#else
    vTaskDelayUntil(&lastWake, delayTicks);
#endif
#if PROFILE_PERF
    const uint64_t frameEndUs = esp_timer_get_time();
    if (s_renderWindowStartUs == 0) s_renderWindowStartUs = frameStartUs;
    s_renderBusyUs += (frameEndUs - frameStartUs);
    s_renderFrames++;
    const uint64_t windowUs = frameEndUs - s_renderWindowStartUs;
    if (windowUs >= (uint64_t)PROFILE_INTERVAL_MS * 1000ULL) {
      const float duty = (windowUs > 0) ? (100.0f * (float)s_renderBusyUs / (float)windowUs) : 0.0f;
      const float fps = (windowUs > 0) ? (1e6f * (float)s_renderFrames / (float)windowUs) : 0.0f;
      AnimationSchedulerTelemetry sched = {};
      getAnimationSchedulerTelemetry(&sched);
      ESP_LOGI(TAG,
               "render duty=%.1f%% fps=%.1f spawn=%s fallback=%ums",
               duty,
               fps,
               waveSpawnReasonName(sched.lastSpawnReason),
               (unsigned)sched.lastFallbackIntervalMs);
      s_renderBusyUs = 0;
      s_renderFrames = 0;
      s_renderWindowStartUs = frameEndUs;
    }
#endif
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Starting ESP-IDF animation port...");

#if ENABLE_DYNAMIC_FREQ_SCALING
  esp_pm_config_t pm_config = {
    .max_freq_mhz = PM_MAX_CPU_MHZ,
    .min_freq_mhz = PM_MIN_CPU_MHZ,
    .light_sleep_enable = false,
  };
  esp_err_t pm_err = esp_pm_configure(&pm_config);
  if (pm_err != ESP_OK) {
    ESP_LOGW(TAG, "Power management not enabled (%s)", esp_err_to_name(pm_err));
  }
#endif

  ESP_ERROR_CHECK(led_strip_device_init(&s_brain, DATA_PIN1, NUM_LEDS1));
#if ENABLE_HAIR_STRIP
  ESP_ERROR_CHECK(led_strip_device_init(&s_hair, DATA_PIN2, NUM_LEDS2));
#endif

  s_brain_buf = (Rgb*)led_strip_device_get_write_buffer(&s_brain);
  if (s_brain_buf) {
    fill_solid(s_brain_buf, NUM_LEDS1, {0, 0, 0});
  }
#if ENABLE_HAIR_STRIP
  s_hair_buf = (Rgb*)led_strip_device_get_write_buffer(&s_hair);
  if (s_hair_buf) {
    fill_solid(s_hair_buf, NUM_LEDS2, {0, 0, 0});
  }
#endif
  led_strip_device_show(&s_brain);
#if ENABLE_HAIR_STRIP
  led_strip_device_show(&s_hair);
#endif

  animationEngineInit(s_brain_buf, NUM_LEDS1);
  resetWaves();
  waveformInitFalloff();
  // Keep wave motion calibration stable regardless of render FPS target.
  setWaveSpeedBaseFps(WAVE_SPEED_BASE_FPS);
  animationEngineReset(now_ms());

  normalizeConfig();
  applyAnimationConfig();
  applyBeatConfig();

  setupI2S();
  audioSchedulerInit();

  const BaseType_t ok = xTaskCreatePinnedToCore(render_task, "render", 6144, NULL, RENDER_TASK_PRIORITY, NULL, RENDER_TASK_CORE);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create render task");
  }
}
