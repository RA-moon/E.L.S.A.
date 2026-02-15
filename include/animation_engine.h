#pragma once

#include <cstdint>
#include <FastLED.h>

struct BeatTelemetry;

void animationEngineInit(CRGB* leds, uint16_t ledCount, BeatTelemetry* telemetry);
void animationEngineReset(uint32_t nowMs);
void runLedAnimation();
