#include "animation_manager.h"
#include "animated_circles.h"
#include "animated_lines.h"
#include "animated_circles_reversed.h"
#include "animated_lines_reversed.h"

#include "esp_timer.h"
#include <algorithm>
#include <math.h>

using FrameFunction = std::vector<std::vector<int>>(*)();

static inline uint32_t now_ms() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static unsigned long lastSwitchTime = 0;
static int currentAnimation = 0;
static int fixedAnimation = 0;
static bool autoMode = true;
static std::vector<std::vector<int>> activeFrames;
static float s_lastBpm = 0.0f;
static float s_lastSwitchBpm = 0.0f;

static constexpr float kBpmSwitchThreshold = 0.05f; // 5%
static constexpr unsigned long kBpmSwitchMinIntervalMs = 3000;
static constexpr unsigned long kAutoSwitchIntervalMs = 30000;

static FrameFunction animations[] = {
    getAnimationFramesCircles,
    getAnimationFramesLines,
    getAnimationFramesCirclesReversed,
    getAnimationFramesLinesReversed,
};

static const char* animationNames[] = {
    "circles",
    "lines",
    "circles-reversed",
    "lines-reversed",
};

int getAnimationCount() {
    return (int)(sizeof(animationNames) / sizeof(animationNames[0]));
}

const char* getAnimationNameByIndex(int index) {
    const int count = getAnimationCount();
    if (index < 0 || index >= count) return "unknown";
    return animationNames[index];
}

const std::vector<std::vector<int>>& getCurrentAnimationFrames() {
    return activeFrames;
}

int getCurrentAnimationIndex() {
    return currentAnimation;
}

const char* getCurrentAnimationName() {
    const int count = getAnimationCount();
    if (currentAnimation < 0 || currentAnimation >= count) return "unknown";
    return animationNames[currentAnimation];
}

bool isAnimationAutoMode() {
    return autoMode;
}

void setAnimationAutoMode(bool enabled) {
    autoMode = enabled;
}

void setAutoSwitchBpm(float bpm) {
    s_lastBpm = bpm;
}

void setAnimationIndex(int index) {
    const int count = getAnimationCount();
    if (count <= 0) return;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    fixedAnimation = index;
    if (!autoMode && currentAnimation != fixedAnimation) {
        currentAnimation = fixedAnimation;
        activeFrames = animations[currentAnimation]();
    }
}

void updateAnimationSwitch() {
    const unsigned long now = now_ms();
    const int count = getAnimationCount();
    if (count <= 0) return;

    if (activeFrames.empty()) {
        currentAnimation = autoMode ? 0 : fixedAnimation;
        if (currentAnimation < 0 || currentAnimation >= count) currentAnimation = 0;
        activeFrames = animations[currentAnimation]();
        lastSwitchTime = now;
        s_lastSwitchBpm = s_lastBpm;
        return;
    }

    if (!autoMode) {
        if (currentAnimation != fixedAnimation) {
            currentAnimation = fixedAnimation;
            activeFrames = animations[currentAnimation]();
            lastSwitchTime = now;
            s_lastSwitchBpm = s_lastBpm;
        }
        return;
    }

    if (s_lastBpm > 0.0f && s_lastSwitchBpm > 0.0f) {
        const float diff = fabsf(s_lastBpm - s_lastSwitchBpm) / s_lastSwitchBpm;
        if (diff >= kBpmSwitchThreshold && (now - lastSwitchTime) >= kBpmSwitchMinIntervalMs) {
            lastSwitchTime = now;
            currentAnimation = (currentAnimation + 1) % count;
            activeFrames = animations[currentAnimation]();
            s_lastSwitchBpm = s_lastBpm;
            return;
        }
    }

    if ((now - lastSwitchTime) >= kAutoSwitchIntervalMs) {
        lastSwitchTime = now;
        currentAnimation = (currentAnimation + 1) % count;
        activeFrames = animations[currentAnimation]();
        s_lastSwitchBpm = s_lastBpm;
    }
}
