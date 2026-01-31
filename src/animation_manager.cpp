#include "animation_manager.h"
#include "animtated_circles.h"
#include "animtated_lines.h"
#include "animtated_circles_reversed.h"
#include "animtated_lines_reversed.h"
#include <Arduino.h>

using FrameFunction = std::vector<std::vector<int>>(*)();

static unsigned long lastSwitchTime = 0;
static int currentAnimation = 0;
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

const std::vector<std::vector<int>>& getCurrentAnimationFrames() {
    return activeFrames;
}

int getCurrentAnimationIndex() {
    return currentAnimation;
}

const char* getCurrentAnimationName() {
    const size_t count = sizeof(animationNames) / sizeof(animationNames[0]);
    if (currentAnimation < 0 || (size_t)currentAnimation >= count) return "unknown";
    return animationNames[currentAnimation];
}

void updateAnimationSwitch() {
    const unsigned long now = millis();

    // Initialize on first call.
    if (activeFrames.empty()) {
        currentAnimation = 0;
        activeFrames = animations[currentAnimation]();
        lastSwitchTime = now;
        return;
    }

    // Switch every 10 seconds.
    if (now - lastSwitchTime >= 10000UL) {
        lastSwitchTime = now;
        currentAnimation = (currentAnimation + 1) % (sizeof(animations) / sizeof(animations[0]));
        activeFrames = animations[currentAnimation]();
    }
}
