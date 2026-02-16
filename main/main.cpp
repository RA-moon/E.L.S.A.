#include "animation_engine.h"
#include "audio_processor.h"
#include "audio_scheduler.h"
#include "elsa_config.h"
#include "hair_strip.h"
#include "led_strip_driver.h"
#include "led_utils.h"
#include "runtime_config.h"
#include "wave_position.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "elsa-idf";

static inline uint32_t now_ms() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static led_strip_device_t s_brain = {};
#if ENABLE_HAIR_STRIP
static led_strip_device_t s_hair = {};
#endif

static Rgb s_brain_buf[NUM_LEDS1];
#if ENABLE_HAIR_STRIP
static Rgb s_hair_buf[NUM_LEDS2];
#endif

static inline bool copyRgbToDevice(const Rgb* src, led_strip_device_t* dev) {
  if (!src || !dev || !dev->pixels) return false;
  uint8_t* out = dev->pixels;
  bool dirty = false;
  for (uint16_t i = 0; i < dev->length; i++) {
    const Rgb c = src[i];
    // WS2812 expects GRB order.
    const uint8_t g = c.g;
    const uint8_t r = c.r;
    const uint8_t b = c.b;
    if (out[0] != g || out[1] != r || out[2] != b) {
      dirty = true;
      out[0] = g;
      out[1] = r;
      out[2] = b;
    }
    out += 3;
  }
  return dirty;
}

static void showStrips() {
  const bool brain_dirty = copyRgbToDevice(s_brain_buf, &s_brain);
  if (brain_dirty) {
    led_strip_device_show(&s_brain);
  }
#if ENABLE_HAIR_STRIP
  const bool hair_dirty = copyRgbToDevice(s_hair_buf, &s_hair);
  if (hair_dirty) {
    led_strip_device_show(&s_hair);
  }
#endif
}

static void render_task(void* arg) {
  (void)arg;
  const TickType_t delayTicks = pdMS_TO_TICKS(DELAY_MS);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    const uint32_t now = now_ms();

#if TEST_SOLID_COLOR
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
    for (uint16_t i = 0; i < NUM_LEDS2; i++) {
      s_hair_buf[i] = color;
    }
#endif
    showStrips();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
    continue;
#endif

    audioSchedulerRun(now, nullptr);

    runLedAnimation(now);
#if ENABLE_HAIR_STRIP
    updateHairStrip(now, s_hair_buf, NUM_LEDS2);
#endif
    showStrips();
    vTaskDelayUntil(&lastWake, delayTicks);
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

  fill_solid(s_brain_buf, NUM_LEDS1, {0, 0, 0});
#if ENABLE_HAIR_STRIP
  fill_solid(s_hair_buf, NUM_LEDS2, {0, 0, 0});
#endif
  showStrips();

  animationEngineInit(s_brain_buf, NUM_LEDS1);
  resetWaves();
  setWaveSpeedBaseFps(1000.0f / (float)DELAY_MS);
  animationEngineReset(now_ms());

  normalizeConfig();
  applyAnimationConfig();
  applyBeatConfig();

  setupI2S();
  audioSchedulerInit();

  const BaseType_t ok = xTaskCreatePinnedToCore(render_task, "render", 6144, NULL, 4, NULL, 1);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create render task");
  }
}
