#pragma once

#include <stdint.h>
#include "led_utils.h"

void updateHairStrip(uint32_t nowMs, Rgb* leds, int ledCount);
