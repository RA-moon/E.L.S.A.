#pragma once

#include <cstdint>
#include <FastLED.h>
#include "elsa_config.h"

struct RuntimeConfig;
struct BeatTelemetry;

void webTelemetryInit(RuntimeConfig* config,
                      BeatTelemetry* telemetry,
                      CRGB* leds,
                      uint16_t ledCount,
                      bool* wifiConnected);

#if ENABLE_WEB_TELEMETRY
void webTelemetrySetup();
void webTelemetryPoll();
#else
inline void webTelemetrySetup() {}
inline void webTelemetryPoll() {}
#endif
