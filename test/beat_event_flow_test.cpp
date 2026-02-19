#include "beat_scheduler_policy.h"

#include <cstdio>
#include <cstdlib>

static void assertTrue(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    std::exit(1);
  }
}

static void assertReason(WaveSpawnReason got, WaveSpawnReason expected, const char* msg) {
  if (got != expected) {
    std::fprintf(stderr,
                 "FAIL: %s (got=%s expected=%s)\n",
                 msg,
                 waveSpawnReasonName(got),
                 waveSpawnReasonName(expected));
    std::exit(1);
  }
}

int main() {
  {
    const BeatFlowResult out = evaluateBeatFlow({true, true, true, false, false});
    assertTrue(out.emitBeatEvent, "real beat should emit");
    assertReason(out.reason, WaveSpawnReason::RealBeat, "real beat reason");
  }

  {
    const BeatFlowResult out = evaluateBeatFlow({true, false, true, false, false});
    assertTrue(out.emitBeatEvent, "fake beat in fake window should emit");
    assertReason(out.reason, WaveSpawnReason::FakeBeat, "fake beat reason");
  }

  {
    const BeatFlowResult out = evaluateBeatFlow({true, false, false, false, false});
    assertTrue(!out.emitBeatEvent, "non-real beat outside fake window must be ignored");
    assertReason(out.reason, WaveSpawnReason::None, "ignored beat reason");
  }

  {
    const BeatFlowResult out = evaluateBeatFlow({false, false, true, true, false});
    assertTrue(out.emitBeatEvent, "synthetic fake due should emit");
    assertReason(out.reason, WaveSpawnReason::FakeBeat, "synthetic fake reason");
  }

  {
    const BeatFlowResult out = evaluateBeatFlow({false, false, false, false, true});
    assertTrue(out.emitBeatEvent, "relax due should emit");
    assertReason(out.reason, WaveSpawnReason::RelaxTick, "relax reason");
  }

  {
    RelaxTickState state = {0, 0};
    const RelaxTickResult t1 = evaluateRelaxTick({1100, 1000, 500.0f}, state);
    assertTrue(!t1.fired, "first relax eval should schedule only");
    assertTrue(t1.state.periodMs == 500, "relax period should match rounded beat period");
    assertTrue(t1.state.nextDueMs == 1500, "first relax due should anchor from lastWaveTime");

    const RelaxTickResult t2 = evaluateRelaxTick({1499, 1000, 500.0f}, t1.state);
    assertTrue(!t2.fired, "relax should not fire early");
    assertTrue(t2.state.nextDueMs == 1500, "next due unchanged before boundary");

    const RelaxTickResult t3 = evaluateRelaxTick({1500, 1000, 500.0f}, t2.state);
    assertTrue(t3.fired, "relax should fire at boundary");
    assertTrue(t3.state.nextDueMs == 2000, "next due advances one period after firing");
    const BeatFlowResult flow3 = evaluateBeatFlow({false, false, false, false, t3.fired});
    assertReason(flow3.reason, WaveSpawnReason::RelaxTick, "relax flow reason");

    const RelaxTickResult t4 = evaluateRelaxTick({2600, 1000, 500.0f}, t3.state);
    assertTrue(t4.fired, "relax should fire when overdue");
    assertTrue(t4.state.nextDueMs == 3000, "relax should catch up without cadence drift");
  }

  std::printf("beat_event_flow_test: OK\n");
  return 0;
}
