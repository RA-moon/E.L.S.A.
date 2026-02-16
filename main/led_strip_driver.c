#include "led_strip_driver.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "led_strip_encoder.h"

static const uint32_t kRmtResolutionHz = 10 * 1000 * 1000; // 10MHz

static uint32_t select_mem_block_symbols(uint16_t length) {
  if (length <= 64) return 64;
  if (length <= 128) return 128;
  return 256;
}

static uint32_t select_queue_depth(uint16_t length) {
  return (length <= 128) ? 2 : 4;
}

static bool IRAM_ATTR led_strip_tx_done_callback(rmt_channel_handle_t channel,
                                                 const rmt_tx_done_event_data_t* edata,
                                                 void* user_ctx) {
  (void)channel;
  (void)edata;
  led_strip_device_t* dev = (led_strip_device_t*)user_ctx;
  if (dev) {
    dev->in_flight = false;
  }
  return false;
}

esp_err_t led_strip_device_init(led_strip_device_t* dev, int gpio, uint16_t length) {
  if (!dev || length == 0) return ESP_ERR_INVALID_ARG;

  dev->length = length;
  dev->bytes = (size_t)length * 3;
  dev->buffers[0] = (uint8_t*)calloc(dev->bytes, 1);
  dev->buffers[1] = (uint8_t*)calloc(dev->bytes, 1);
  if (!dev->buffers[0] || !dev->buffers[1]) {
    return ESP_ERR_NO_MEM;
  }
  dev->active_index = 0;
  dev->write_index = 1;
  dev->in_flight = false;

  rmt_tx_channel_config_t tx_cfg = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .gpio_num = gpio,
    .mem_block_symbols = select_mem_block_symbols(length),
    .resolution_hz = kRmtResolutionHz,
    .trans_queue_depth = select_queue_depth(length),
  };

  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &dev->channel));

  led_strip_encoder_config_t enc_cfg = {
    .resolution = kRmtResolutionHz,
  };
  ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&enc_cfg, &dev->encoder));
  rmt_tx_event_callbacks_t cbs = {
    .on_trans_done = led_strip_tx_done_callback,
  };
  ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(dev->channel, &cbs, dev));
  ESP_ERROR_CHECK(rmt_enable(dev->channel));
  return ESP_OK;
}

void led_strip_device_clear(led_strip_device_t* dev) {
  if (!dev) return;
  if (dev->buffers[0]) memset(dev->buffers[0], 0, dev->bytes);
  if (dev->buffers[1]) memset(dev->buffers[1], 0, dev->bytes);
}

void led_strip_device_set_rgb(led_strip_device_t* dev, uint16_t idx, uint8_t r, uint8_t g, uint8_t b) {
  if (!dev || idx >= dev->length) return;
  uint8_t* buf = led_strip_device_get_write_buffer(dev);
  if (!buf) return;
  const size_t base = (size_t)idx * 3;
  // WS2812 expects GRB order.
  buf[base + 0] = g;
  buf[base + 1] = r;
  buf[base + 2] = b;
}

uint8_t* led_strip_device_get_write_buffer(led_strip_device_t* dev) {
  if (!dev) return NULL;
  return dev->buffers[dev->write_index];
}

bool led_strip_device_show_async(led_strip_device_t* dev) {
  if (!dev || !dev->channel || !dev->encoder) return false;
  if (dev->in_flight) return false;
  dev->active_index = dev->write_index;
  dev->write_index ^= 1;
  dev->in_flight = true;
  rmt_transmit_config_t tx_cfg = {
    .loop_count = 0,
  };
  ESP_ERROR_CHECK(rmt_transmit(dev->channel, dev->encoder, dev->buffers[dev->active_index], dev->bytes, &tx_cfg));
  return true;
}

void led_strip_device_show(led_strip_device_t* dev) {
  if (!dev) return;
  if (!led_strip_device_show_async(dev)) return;
  ESP_ERROR_CHECK(rmt_tx_wait_all_done(dev->channel, portMAX_DELAY));
}
