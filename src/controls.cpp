#include "controls.h"

#include <Arduino.h>

#include "animation_manager.h"
#include "elsa_config.h"
#include "runtime_config.h"

static bool s_buttonStable = false;
static bool s_buttonLastRead = false;
static uint32_t s_buttonLastChangeMs = 0;
static uint32_t s_buttonLastTapMs = 0;
static bool s_buttonWaitingSecondTap = false;

void setupButton() {
  pinMode(BUTTON_PIN, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
}

void handleButton() {
  const uint32_t now = millis();
  const bool raw = (digitalRead(BUTTON_PIN) == (BUTTON_ACTIVE_LOW ? LOW : HIGH));

  if (raw != s_buttonLastRead) {
    s_buttonLastRead = raw;
    s_buttonLastChangeMs = now;
  }

  if ((now - s_buttonLastChangeMs) >= BUTTON_DEBOUNCE_MS && s_buttonStable != s_buttonLastRead) {
    s_buttonStable = s_buttonLastRead;
    if (s_buttonStable) {
      if (s_buttonWaitingSecondTap && (now - s_buttonLastTapMs) <= BUTTON_DOUBLE_TAP_MS) {
        s_buttonWaitingSecondTap = false;
        g_config.animationAuto = !g_config.animationAuto;
        normalizeConfig();
        applyAnimationConfig();
      } else {
        s_buttonWaitingSecondTap = true;
        s_buttonLastTapMs = now;
      }
    }
  }

  if (s_buttonWaitingSecondTap && (now - s_buttonLastTapMs) > BUTTON_DOUBLE_TAP_MS) {
    s_buttonWaitingSecondTap = false;
    if (!g_config.animationAuto) {
      const int count = getAnimationCount();
      if (count > 0) {
        g_config.animationIndex = (g_config.animationIndex + 1) % count;
        normalizeConfig();
        applyAnimationConfig();
      }
    }
  }
}
