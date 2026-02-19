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
  {
    const FallbackPolicyInput in = {
      2000,  // nowMs
      800,   // fallbackMs
      true,  // enableBeatWaves
      500.0f,
      430,
      800,
      1400,  // lastRealBeatMs
      0,     // startupMs
      0,     // lastBeatMs
      1000,  // lastWaveTimeMs
      1.2f
    };
    const FallbackPolicyResult out = evaluateFallbackPolicy(in);
    assertTrue(out.intervalMs == 600, "overdue interval must be avg*1.2");
    assertTrue(out.beatQuiet, "real beat should be considered quiet at overdue boundary");
    assertTrue(out.waveQuiet, "wave should be quiet");
    assertTrue(out.shouldSpawn, "fallback should spawn when beat+wave are quiet");
  }

  {
    const FallbackPolicyInput in = {
      1700,
      800,
      true,
      500.0f,
      430,
      800,
      0,     // no real beat yet
      1000,  // startupMs
      0,
      900,
      1.2f
    };
    const FallbackPolicyResult out = evaluateFallbackPolicy(in);
    assertTrue(!out.beatQuiet, "bootstrap should wait fallbackMs before first spawn");
    assertTrue(!out.shouldSpawn, "no spawn before bootstrap delay");
  }

  {
    const FallbackPolicyInput in = {
      1800,
      800,
      true,
      500.0f,
      430,
      800,
      0,
      1000,
      0,
      900,
      1.2f
    };
    const FallbackPolicyResult out = evaluateFallbackPolicy(in);
    assertTrue(out.beatQuiet, "bootstrap should become quiet at fallbackMs boundary");
    assertTrue(out.waveQuiet, "wave should be quiet at overdue interval");
    assertTrue(out.shouldSpawn, "spawn expected once bootstrap delay is reached");
  }

  {
    const FallbackPolicyInput in = {
      1700,
      800,
      true,
      500.0f,
      430,
      800,
      1000,
      0,
      0,
      1300,  // too recent wave
      1.2f
    };
    const FallbackPolicyResult out = evaluateFallbackPolicy(in);
    assertTrue(out.beatQuiet, "beat quiet expected");
    assertTrue(!out.waveQuiet, "wave must block spawn until quiet");
    assertTrue(!out.shouldSpawn, "no spawn while wave is not quiet");
  }

  {
    const FallbackPolicyInput in = {
      1800,
      800,
      false, // beat waves disabled
      500.0f,
      430,
      800,
      0,
      0,
      1000,  // use lastBeatMs in fixed mode when no real beat
      1000,
      1.2f
    };
    const FallbackPolicyResult out = evaluateFallbackPolicy(in);
    assertTrue(out.intervalMs == 800, "fixed mode should use fallbackMs");
    assertTrue(out.shouldSpawn, "fixed mode should spawn at fallback boundary");
  }

  {
    const FallbackPolicyInput in = {
      1001,
      0,     // must be hardened to 1ms minimum
      false, // beat waves disabled
      500.0f,
      430,
      800,
      0,
      0,
      1000,
      1000,
      1.2f
    };
    const FallbackPolicyResult out = evaluateFallbackPolicy(in);
    assertTrue(out.intervalMs == 1, "fallback interval should clamp to 1ms minimum");
    assertTrue(out.shouldSpawn, "minimum interval should still produce deterministic spawn");
  }

  assertTrue(waveSpawnReasonName(WaveSpawnReason::RealBeat)[0] == 'r', "reason mapping real");
  assertTrue(waveSpawnReasonName(WaveSpawnReason::FakeBeat)[0] == 'f', "reason mapping fake");
  assertTrue(waveSpawnReasonName(WaveSpawnReason::RelaxTick)[0] == 'r', "reason mapping relax");
  assertTrue(waveSpawnReasonName(WaveSpawnReason::Fallback)[0] == 'f', "reason mapping fallback");
  assertTrue(waveSpawnReasonName(WaveSpawnReason::None)[0] == 'n', "reason mapping none");

  std::printf("no_beat_policy_test: OK\n");
  return 0;
}
