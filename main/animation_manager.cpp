#include "animation_manager.h"
#include "animated_circles.h"
#include "animated_lines.h"
#include "animated_circles_reversed.h"
#include "animated_lines_reversed.h"
#include "elsa_config.h"

#include "esp_random.h"
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
static uint32_t s_bpmWindowStartMs = 0;
static float s_bpmWindowStartBpm = 0.0f;

static int pickRandomAnimation(int count, int current) {
    if (count <= 1) return current;
    int next = current;
    while (next == current) {
        next = (int)(esp_random() % (uint32_t)count);
    }
    return next;
}

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
        s_bpmWindowStartMs = now;
        s_bpmWindowStartBpm = s_lastBpm;
        return;
    }

    if (!autoMode) {
        if (currentAnimation != fixedAnimation) {
            currentAnimation = fixedAnimation;
            activeFrames = animations[currentAnimation]();
            lastSwitchTime = now;
            s_bpmWindowStartMs = now;
            s_bpmWindowStartBpm = s_lastBpm;
        }
        return;
    }

    if (s_lastBpm > 0.0f) {
        if (s_bpmWindowStartMs == 0 || s_bpmWindowStartBpm <= 0.0f) {
            s_bpmWindowStartMs = now;
            s_bpmWindowStartBpm = s_lastBpm;
        } else if ((now - s_bpmWindowStartMs) >= BPM_SWITCH_WINDOW_MS) {
            const float diff = fabsf(s_lastBpm - s_bpmWindowStartBpm) / s_bpmWindowStartBpm;
            s_bpmWindowStartMs = now;
            s_bpmWindowStartBpm = s_lastBpm;
            if (diff >= BPM_SWITCH_THRESHOLD) {
                lastSwitchTime = now;
                currentAnimation = pickRandomAnimation(count, currentAnimation);
                activeFrames = animations[currentAnimation]();
                return;
            }
        }
    } else {
        s_bpmWindowStartMs = 0;
        s_bpmWindowStartBpm = 0.0f;
    }

    if ((now - lastSwitchTime) >= AUTO_SWITCH_INTERVAL_MS) {
        lastSwitchTime = now;
        currentAnimation = pickRandomAnimation(count, currentAnimation);
        activeFrames = animations[currentAnimation]();
    }
}
