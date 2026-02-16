#pragma once

#include <cstdint>

void audioSchedulerInit();
bool audioSchedulerRun(uint32_t nowMs, uint32_t* outDurationUs);
