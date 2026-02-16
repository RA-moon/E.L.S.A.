#pragma once

#include <cstdint>

struct RuntimeConfig {
  uint8_t brightness;
  uint16_t beatDecayMinMs;
  uint16_t beatDecayMaxMs;
  int16_t pulseLeadMs;
  uint16_t fallbackMs;
  uint8_t maxActiveWaves;
  bool enableBeatWaves;
  bool enableFallbackWaves;
  bool animationAuto;
  int animationIndex;
  float energyEmaAlpha;
  float fluxEmaAlpha;
  float fluxThreshold;
  float fluxRiseFactor;
  uint16_t minBeatIntervalMs;
  uint16_t avgBeatMinMs;
  uint16_t avgBeatMaxMs;
};

extern RuntimeConfig g_config;

void normalizeConfig();
void applyAnimationConfig();
void applyBeatConfig();
