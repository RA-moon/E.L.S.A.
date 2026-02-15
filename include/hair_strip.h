#pragma once

#include <Arduino.h>
#include <FastLED.h>

void updateHairStrip(uint32_t nowMs, CRGB* leds, int ledCount);
