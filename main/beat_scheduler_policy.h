#pragma once

#include <cstdint>

enum class WaveSpawnReason : uint8_t {
  None = 0,
  RealBeat = 1,
  FakeBeat = 2,
  RelaxTick = 3,
  Fallback = 4,
};

struct FallbackPolicyInput {
  uint32_t nowMs;
  uint32_t fallbackMs;
  bool enableBeatWaves;
  float averageBeatMs;
  uint16_t avgBeatMinMs;
  uint16_t avgBeatMaxMs;
  uint32_t lastRealBeatMs;
  uint32_t startupMs;
  uint32_t lastBeatMs;
  uint32_t lastWaveTimeMs;
  float overdueRatio;
};

struct FallbackPolicyResult {
  uint32_t intervalMs;
  bool beatQuiet;
  bool waveQuiet;
  bool shouldSpawn;
};

struct BeatFlowInput {
  bool beatConsumed;
  bool beatIsReal;
  bool inFakeWindow;
  bool syntheticFakeDue;
  bool relaxDue;
};

struct BeatFlowResult {
  bool emitBeatEvent;
  WaveSpawnReason reason;
};

struct RelaxTickState {
  uint32_t nextDueMs;
  uint32_t periodMs;
};

struct RelaxTickInput {
  uint32_t nowMs;
  uint32_t lastWaveTimeMs;
  float beatPeriodMs;
};

struct RelaxTickResult {
  bool fired;
  RelaxTickState state;
};

FallbackPolicyResult evaluateFallbackPolicy(const FallbackPolicyInput& input);
BeatFlowResult evaluateBeatFlow(const BeatFlowInput& input);
RelaxTickResult evaluateRelaxTick(const RelaxTickInput& input, const RelaxTickState& state);
const char* waveSpawnReasonName(WaveSpawnReason reason);
