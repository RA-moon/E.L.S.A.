#pragma once
#include <vector>
#include <stdint.h>

const std::vector<std::vector<int>>& getCurrentAnimationFrames();
void updateAnimationSwitch();
void setAutoSwitchBpm(float bpm);
int getCurrentAnimationIndex();
const char* getCurrentAnimationName();
int getAnimationCount();
const char* getAnimationNameByIndex(int index);
bool isAnimationAutoMode();
void setAnimationAutoMode(bool enabled);
void setAnimationIndex(int index);
