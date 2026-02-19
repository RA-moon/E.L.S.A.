#include "beat_scheduler_policy.h"

#include <cstdio>
#include <cstdlib>

static void assertTrue(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    std::exit(1);
  }
}

int main() {
  constexpr uint32_t kFakeWindowMs = 10000;
  constexpr uint32_t kStartupMs = 1000;
  constexpr uint32_t kFallbackMs = 800;
  constexpr float kAverageBeatMs = 500.0f;
  constexpr uint16_t kAvgBeatMinMs = 430;
  constexpr uint16_t kAvgBeatMaxMs = 800;
  constexpr float kOverdueRatio = 1.2f;

  uint32_t lastRealBeatMs = kStartupMs;
  uint32_t lastBeatMs = kStartupMs;
  uint32_t lastWaveTimeMs = kStartupMs;
  uint32_t lastSyntheticBeatMs = kStartupMs;
  RelaxTickState relaxState = {0, 0};

  int fakeSpawns = 0;
  int relaxSpawnsAfterWindow = 0;
  int fallbackSpawnsAfterWindow = 0;

  for (uint32_t now = 1100; now <= 16000; now += 100) {
    const uint32_t refBeatMs = (lastRealBeatMs > 0) ? lastRealBeatMs : kStartupMs;
    const uint32_t idleMs = now - refBeatMs;
    const bool inFakeWindow = (idleMs < kFakeWindowMs);

    const bool beatConsumed = false;
    const bool beatIsReal = false;
    const bool syntheticFakeDue = inFakeWindow &&
      (lastSyntheticBeatMs == 0 || (now - lastSyntheticBeatMs) >= (uint32_t)kAverageBeatMs);

    bool beatEvent = false;
    WaveSpawnReason reason = WaveSpawnReason::None;

    const BeatFlowResult beatFlow = evaluateBeatFlow({
      beatConsumed,
      beatIsReal,
      inFakeWindow,
      syntheticFakeDue,
      false
    });
    if (beatFlow.emitBeatEvent) {
      beatEvent = true;
      reason = beatFlow.reason;
      if (reason == WaveSpawnReason::FakeBeat) {
        fakeSpawns++;
        lastSyntheticBeatMs = now;
        lastBeatMs = now;
      }
    }

    const bool fadeActive = (idleMs > kFakeWindowMs);
    if (fadeActive && !beatEvent) {
      const RelaxTickResult relax = evaluateRelaxTick(
        {
          now,
          lastWaveTimeMs,
          kAverageBeatMs,
        },
        relaxState
      );
      relaxState = relax.state;
      const BeatFlowResult relaxFlow = evaluateBeatFlow({
        false,
        false,
        false,
        false,
        relax.fired
      });
      if (relaxFlow.emitBeatEvent) {
        beatEvent = true;
        reason = relaxFlow.reason;
        lastBeatMs = now;
        lastSyntheticBeatMs = now;
      }
    } else if (!fadeActive) {
      relaxState = {0, 0};
    }

    if (beatEvent) {
      lastWaveTimeMs = now;
      if (idleMs >= kFakeWindowMs && reason == WaveSpawnReason::RelaxTick) {
        relaxSpawnsAfterWindow++;
      }
    }

    const FallbackPolicyResult fallback = evaluateFallbackPolicy({
      now,
      kFallbackMs,
      true,
      kAverageBeatMs,
      kAvgBeatMinMs,
      kAvgBeatMaxMs,
      lastRealBeatMs,
      kStartupMs,
      lastBeatMs,
      lastWaveTimeMs,
      kOverdueRatio,
    });

    if (fallback.shouldSpawn) {
      if (idleMs >= kFakeWindowMs) {
        fallbackSpawnsAfterWindow++;
      }
      lastWaveTimeMs = now;
    }
  }

  assertTrue(fakeSpawns > 0, "fake window should emit synthetic beats");
  assertTrue(relaxSpawnsAfterWindow > 0, "relax ticks should emit after fake window");
  assertTrue(fallbackSpawnsAfterWindow == 0,
             "fallback should stay suppressed when relax ticks keep cadence tighter than overdue interval");

  std::printf("beat_scheduler_integration_test: OK\n");
  return 0;
}
