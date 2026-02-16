#include "led_strip_driver.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "led_strip_encoder.h"

static const uint32_t kRmtResolutionHz = 10 * 1000 * 1000; // 10MHz

esp_err_t led_strip_device_init(led_strip_device_t* dev, int gpio, uint16_t length) {
  if (!dev || length == 0) return ESP_ERR_INVALID_ARG;

  dev->length = length;
  dev->bytes = (size_t)length * 3;
  dev->pixels = (uint8_t*)calloc(dev->bytes, 1);
  if (!dev->pixels) {
    return ESP_ERR_NO_MEM;
  }

  rmt_tx_channel_config_t tx_cfg = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .gpio_num = gpio,
    .mem_block_symbols = 64,
    .resolution_hz = kRmtResolutionHz,
    .trans_queue_depth = 4,
  };

  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &dev->channel));

  led_strip_encoder_config_t enc_cfg = {
    .resolution = kRmtResolutionHz,
  };
  ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&enc_cfg, &dev->encoder));
  ESP_ERROR_CHECK(rmt_enable(dev->channel));
  return ESP_OK;
}

void led_strip_device_clear(led_strip_device_t* dev) {
  if (!dev || !dev->pixels) return;
  memset(dev->pixels, 0, dev->bytes);
}

void led_strip_device_set_rgb(led_strip_device_t* dev, uint16_t idx, uint8_t r, uint8_t g, uint8_t b) {
  if (!dev || !dev->pixels || idx >= dev->length) return;
  const size_t base = (size_t)idx * 3;
  // WS2812 expects GRB order.
  dev->pixels[base + 0] = g;
  dev->pixels[base + 1] = r;
  dev->pixels[base + 2] = b;
}

void led_strip_device_show(led_strip_device_t* dev) {
  if (!dev || !dev->pixels || !dev->channel || !dev->encoder) return;
  rmt_transmit_config_t tx_cfg = {
    .loop_count = 0,
  };
  ESP_ERROR_CHECK(rmt_transmit(dev->channel, dev->encoder, dev->pixels, dev->bytes, &tx_cfg));
  ESP_ERROR_CHECK(rmt_tx_wait_all_done(dev->channel, portMAX_DELAY));
}
