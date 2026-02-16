#pragma once

#include <stdint.h>
#include "led_utils.h"

void animationEngineInit(Rgb* leds, uint16_t ledCount);
void animationEngineReset(uint32_t nowMs);
void runLedAnimation(uint32_t nowMs);
