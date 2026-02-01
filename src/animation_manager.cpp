#include "animation_manager.h"
#include "animtated_circles.h"
#include "animtated_lines.h"
#include "animtated_circles_reversed.h"
#include "animtated_lines_reversed.h"
#include <Arduino.h>

using FrameFunction = std::vector<std::vector<int>>(*)();

static unsigned long lastSwitchTime = 0;
static int currentAnimation = 0;
static int fixedAnimation = 0;
static bool autoMode = true;
static std::vector<std::vector<int>> activeFrames;

// Keep this list limited to animations that are present in the project.
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
    const unsigned long now = millis();
    const int count = getAnimationCount();
    if (count <= 0) return;

    // Initialize on first call.
    if (activeFrames.empty()) {
        currentAnimation = autoMode ? 0 : fixedAnimation;
        if (currentAnimation < 0 || currentAnimation >= count) currentAnimation = 0;
        activeFrames = animations[currentAnimation]();
        lastSwitchTime = now;
        return;
    }

    if (!autoMode) {
        if (currentAnimation != fixedAnimation) {
            currentAnimation = fixedAnimation;
            activeFrames = animations[currentAnimation]();
        }
        return;
    }

    // Switch every 10 seconds.
    if (now - lastSwitchTime >= 10000UL) {
        lastSwitchTime = now;
        currentAnimation = (currentAnimation + 1) % count;
        activeFrames = animations[currentAnimation]();
    }
}
