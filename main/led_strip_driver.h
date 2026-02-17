#pragma once

#include <stdint.h>
#include "driver/rmt_tx.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  rmt_channel_handle_t channel;
  rmt_encoder_handle_t encoder;
  uint8_t* buffers[2];
  uint16_t length;
  size_t bytes;
  uint8_t active_index;
  uint8_t write_index;
  volatile bool in_flight;
} led_strip_device_t;

esp_err_t led_strip_device_init(led_strip_device_t* dev, int gpio, uint16_t length);
void led_strip_device_clear(led_strip_device_t* dev);
void led_strip_device_set_rgb(led_strip_device_t* dev, uint16_t idx, uint8_t r, uint8_t g, uint8_t b);
uint8_t* led_strip_device_get_write_buffer(led_strip_device_t* dev);
bool led_strip_device_show_async(led_strip_device_t* dev);
void led_strip_device_show(led_strip_device_t* dev);
bool led_strip_device_is_busy(const led_strip_device_t* dev);

#ifdef __cplusplus
} // extern "C"
#endif
