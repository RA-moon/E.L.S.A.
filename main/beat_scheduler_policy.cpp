#include "beat_scheduler_policy.h"

#include <math.h>

static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline uint32_t clampPeriodMs(float beatPeriodMs) {
  uint32_t periodMs = (uint32_t)lroundf(beatPeriodMs);
  if (periodMs < 1) periodMs = 1;
  return periodMs;
}

FallbackPolicyResult evaluateFallbackPolicy(const FallbackPolicyInput& input) {
  FallbackPolicyResult out = {};
  const uint32_t fallbackIntervalMs = (input.fallbackMs > 0) ? input.fallbackMs : 1;
  out.intervalMs = fallbackIntervalMs;
  out.beatQuiet = false;
  out.waveQuiet = false;
  out.shouldSpawn = false;

  if (!input.enableBeatWaves) {
    const uint32_t refBeatMs = (input.lastRealBeatMs > 0) ? input.lastRealBeatMs : input.lastBeatMs;
    out.beatQuiet = (refBeatMs == 0) ? true : (input.nowMs - refBeatMs >= out.intervalMs);
  } else {
    float expectedMs = clampf(input.averageBeatMs, (float)input.avgBeatMinMs, (float)input.avgBeatMaxMs);
    out.intervalMs = (uint32_t)lroundf(expectedMs * input.overdueRatio);
    if (out.intervalMs < 1) out.intervalMs = 1;

    if (input.lastRealBeatMs > 0) {
      out.beatQuiet = (input.nowMs - input.lastRealBeatMs >= out.intervalMs);
    } else {
      const uint32_t refMs = (input.startupMs > 0) ? input.startupMs : input.lastBeatMs;
      const uint32_t bootstrapMs = fallbackIntervalMs;
      out.beatQuiet = (refMs == 0) ? true : (input.nowMs - refMs >= bootstrapMs);
    }
  }

  out.waveQuiet = (input.nowMs - input.lastWaveTimeMs >= out.intervalMs);
  out.shouldSpawn = out.beatQuiet && out.waveQuiet;
  return out;
}

BeatFlowResult evaluateBeatFlow(const BeatFlowInput& input) {
  BeatFlowResult out = {};
  out.emitBeatEvent = false;
  out.reason = WaveSpawnReason::None;

  if (input.beatConsumed && (input.beatIsReal || input.inFakeWindow)) {
    out.emitBeatEvent = true;
    out.reason = input.beatIsReal ? WaveSpawnReason::RealBeat : WaveSpawnReason::FakeBeat;
    return out;
  }

  if (!input.beatConsumed && input.inFakeWindow && input.syntheticFakeDue) {
    out.emitBeatEvent = true;
    out.reason = WaveSpawnReason::FakeBeat;
    return out;
  }

  if (input.relaxDue) {
    out.emitBeatEvent = true;
    out.reason = WaveSpawnReason::RelaxTick;
  }

  return out;
}

RelaxTickResult evaluateRelaxTick(const RelaxTickInput& input, const RelaxTickState& state) {
  RelaxTickResult out = {};
  out.fired = false;
  out.state = state;

  if (out.state.nextDueMs == 0) {
    out.state.periodMs = clampPeriodMs(input.beatPeriodMs);
    out.state.nextDueMs =
      (input.lastWaveTimeMs > 0) ? (input.lastWaveTimeMs + out.state.periodMs) : (input.nowMs + out.state.periodMs);
  }

  if (out.state.periodMs < 1) out.state.periodMs = 1;

  if ((int32_t)(input.nowMs - out.state.nextDueMs) >= 0) {
    out.fired = true;
    do {
      out.state.nextDueMs += out.state.periodMs;
    } while ((int32_t)(input.nowMs - out.state.nextDueMs) >= 0);
  }

  return out;
}

const char* waveSpawnReasonName(WaveSpawnReason reason) {
  switch (reason) {
    case WaveSpawnReason::RealBeat:
      return "real";
    case WaveSpawnReason::FakeBeat:
      return "fake";
    case WaveSpawnReason::RelaxTick:
      return "relax";
    case WaveSpawnReason::Fallback:
      return "fallback";
    case WaveSpawnReason::None:
    default:
      return "none";
  }
}
