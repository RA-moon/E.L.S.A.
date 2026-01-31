#include "animtated_lines.h"
#include "animtated_lines_reversed.h"
#include <algorithm>

std::vector<std::vector<int>> getAnimationFramesLines() {
    // Build the forward animation by reversing the frame order of the provided
    // "reversed" animation.
    auto frames = getAnimationFramesLinesReversed();
    std::reverse(frames.begin(), frames.end());
    return frames;
}
