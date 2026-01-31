#pragma once
#include <vector>
#include <stdint.h>

const std::vector<std::vector<int>>& getCurrentAnimationFrames();
void updateAnimationSwitch();
int getCurrentAnimationIndex();
const char* getCurrentAnimationName();
