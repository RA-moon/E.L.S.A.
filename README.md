# E.L.S.A. (ESP32-S3 LED + Audio Beat Visualizer)

**E**xperimental **L**ive **S**ynthetic **A**utomation
ESP32-S3 project for driving addressable LEDs with audio-reactive animations (ESP-IDF + RMT + esp-dsp).

## Features
- Audio beat detection (SPH0645 I2S mic)
- Multiple animation patterns (auto or fixed)
- ESP-IDF build (RMT LED output + esp-dsp FFT)
- PlatformIO build + upload

## Hardware (current wiring)
- Board: ESP32-S3 Super Mini (4MB Flash / 2MB PSRAM)
- LED data: GPIO1
- Hair LED data: GPIO2
- I2S mic (SPH0645):
  - BCLK: GPIO5
  - LRCLK/WS: GPIO6
  - DOUT (mic) -> DIN: GPIO7
  - SEL -> GND (left channel)
  - Pins default to `I2S_BCLK_PIN=5`, `I2S_WS_PIN=6`, `I2S_DIN_PIN=7` in `main/audio_processor.cpp`.
    To override, set `-DI2S_BCLK_PIN=...`, `-DI2S_WS_PIN=...`, `-DI2S_DIN_PIN=...` in `platformio.ini`.
- Button (reserved, currently unused): GPIO4

## Wiring Diagram (Full)
```
ESP32-S3 Super Mini
  5V   ---------------------> LED strips +5V (brain + hair)
  3V3  ---------------------> SPH0645 VDD
  GND  ----+---------------> LED strips GND
           +---------------> SPH0645 GND
           +---------------> Power supply GND (common ground)

  GPIO1 --------------------> Brain strip DIN (NUM_LEDS1=120)
  GPIO2 --------------------> Hair strip DIN (NUM_LEDS2=44)

  GPIO5 --------------------> SPH0645 BCLK
  GPIO6 --------------------> SPH0645 LRCLK/WS
  GPIO7 --------------------> SPH0645 DOUT
  SEL  ---------------------> GND (left channel)

  GPIO4 --------------------> Button -> GND (reserved / currently unused)
```

**Notes:**
- Ensure all grounds are common between ESP32, mic, LED strips, and the LED power supply.
- LED strips must connect to their **DIN** (input) end.

## Build + Upload (PlatformIO)
```bash
pio run -e esp32-s3-super-mini-idf
pio run -e esp32-s3-super-mini-idf -t upload
```

If upload is flaky, hold BOOT, tap RESET, then release BOOT after "Connecting..." appears.

### ESP-IDF Environment
The IDF entry point is `main/main.cpp`.
Current status:
- LED output uses an RMT-based WS2812 encoder (`main/led_strip_driver.c`).
- Audio capture uses ESP-IDF I2S and esp-dsp FFT (`main/audio_processor.cpp`).
- The IDF build depends on the esp-dsp component.
  This repo vendors it under `components/esp-dsp` (cloned from Espressif).
  If it’s missing, fetch it with:
  ```bash
  git clone https://github.com/espressif/esp-dsp.git components/esp-dsp
  ```

## Formatting
```bash
./scripts/format.sh
```

## Architecture
See `docs/architecture.md`.

## Audio tuning
Shared defaults live in `main/audio_processor.cpp`:
```c
#define AUDIO_SAMPLE_RATE_HZ 32000
#define AUDIO_FFT_SAMPLES 256
```
Override with `-DAUDIO_SAMPLE_RATE_HZ=...` / `-DAUDIO_FFT_SAMPLES=...` in `platformio.ini`.

## Tunable Settings
Most values are compile-time defines in `main/elsa_config.h` (override with `-D...` in
`platformio.ini`). Additional beat detector defaults live in `main/runtime_config.cpp`.

**Hardware / Pins (`main/elsa_config.h`)**
- `DATA_PIN1`, `NUM_LEDS1`
- `DATA_PIN2`, `NUM_LEDS2`, `ENABLE_HAIR_STRIP`

**LED + Core Animation (`main/elsa_config.h`)**
- `BRIGHTNESS1` (baseline brightness)
- `DELAY_MS` (render loop interval)
- `RENDER_TARGET_FPS` (0 = use `DELAY_MS` tick pacing)
- `SKIP_RENDER_WHEN_BUSY` (0 = stable timing, 1 = skip render while RMT is busy)
- `RENDER_FPS_LOG_MS` (0 = disabled)
- `AUDIO_INTERVAL` (audio task period)
- `MAX_ACTIVE_WAVES`
- `WAVE_SPACING_INTERVAL_MS`
- `WAVE_NOSE_RATIO`, `WAVE_GAP_RATIO` (spacing normalized from `nose + gap + nose`)
- `WAVE_SPEED_BASE`, `WAVE_SPEED_RANGE`, `WAVE_SPEED_MULTIPLIER`, `WAVE_SPEED_BASE_FPS`
- `WAVE_FALLOFF_NOSE_P1`, `WAVE_FALLOFF_NOSE_P2`
- `WAVE_FALLOFF_TAIL_P1`, `WAVE_FALLOFF_TAIL_P2`
- `WAVE_FALLOFF_LUT_ENABLE`, `WAVE_FALLOFF_LUT_SIZE`
- `BPM_SWITCH_THRESHOLD`, `BPM_SWITCH_WINDOW_MS`, `AUTO_SWITCH_INTERVAL_MS`

**Pulse + Beat Response (`main/elsa_config.h`)**
- `BEAT_PULSE_MIN_RATIO`, `BEAT_PULSE_DECAY_RATIO`
- `BEAT_DECAY_MIN_MS`, `BEAT_DECAY_MAX_MS`, `BEAT_DECAY_EASE_OUT`, `BEAT_PERIOD_EMA_ALPHA`
- `FADE_TO_STARTUP_IDLE_MS`, `FADE_TO_STARTUP_DURATION_MS`
- `ENABLE_BEAT_WAVES`, `ENABLE_FALLBACK_WAVES`, `NO_BEAT_FALLBACK_MS`
- `pulseLeadMs` (in `main/runtime_config.cpp`, applied at runtime)

**Hair Strip (`main/elsa_config.h`)**
- `ENABLE_HAIR_STRIP`, `DATA_PIN2`, `NUM_LEDS2`
- `HAIR_BRIGHTNESS`, `HAIR_SPEED_RAINBOW`, `HAIR_SPEED_FADE`, `HAIR_UPDATE_MS`
- `HAIR_COLOR_CYCLE_DURATION_MS`, `HAIR_RAINBOW_END1`, `HAIR_FADE_START`, `HAIR_FADE_END`,
  `HAIR_RAINBOW_START2`, `HAIR_RAINBOW_END2`

**Audio / I2S (`main/audio_processor.cpp`)**
- `I2S_BCLK_PIN`, `I2S_WS_PIN`, `I2S_DIN_PIN`, `I2S_MCLK_PIN`
- `SPH0645_RAW_SHIFT`
- `AUDIO_SAMPLE_RATE_HZ`, `AUDIO_FFT_SAMPLES`

**Beat Detector Defaults (`main/runtime_config.cpp`)**
- `animationAuto`, `animationIndex`
- `brightness` (runtime, seeded by `BRIGHTNESS1`)
- `energyEmaAlpha`, `fluxEmaAlpha`, `fluxThreshold`, `fluxRiseFactor`
- `minBeatIntervalMs`, `avgBeatMinMs`, `avgBeatMaxMs`

**Tasks / Performance (`main/elsa_config.h`)**
- `AUDIO_TASK_ENABLE`, `AUDIO_TASK_PRIORITY`, `AUDIO_TASK_CORE`
- `RENDER_TASK_PRIORITY`, `RENDER_TASK_CORE`
- `PROFILE_PERF`, `PROFILE_INTERVAL_MS`

**Buttons / Debug (`main/elsa_config.h`)**
- `TEST_SOLID_COLOR`, `TEST_LED_COUNT`
- Reserved/commented-out (currently unused): `BUTTON_*`, `DEBUG_*`

**Power (`main/elsa_config.h`)**
- `ENABLE_DYNAMIC_FREQ_SCALING`, `PM_MAX_CPU_MHZ`, `PM_MIN_CPU_MHZ`

**Networking / OTA (`main/elsa_config.h`)**
- Reserved/commented-out (currently unused): `ENABLE_WEB_TELEMETRY`, `ENABLE_CONFIG_ENDPOINT`,
  `WEB_SERVER_PORT`, `WIFI_CONNECT_TIMEOUT_MS`, `FRAME_MIN_INTERVAL_MS`,
  `ENABLE_WIFI_KEEPALIVE`, `WIFI_KEEPALIVE_INTERVAL_MS`, `ENABLE_OTA`

## Notes
- LED output (RMT transmit + buffer copy) is the largest CPU cost.

## Recent Performance/Behavior Changes
- **Time-based wave motion:** wave position now advances by `speed * dt` (seconds), so speed is stable even if FPS jitters.
- **Stable timing default:** `SKIP_RENDER_WHEN_BUSY` now defaults to `0` (do not skip animation updates when RMT is busy).
- **Wave speed base FPS:** wave speed calibration uses `WAVE_SPEED_BASE_FPS` to keep motion consistent across render FPS targets.
- **Event-driven wave spawning:** waves are spawned on beat events (real, fake window, or relaxation ticks) instead of a continuous scheduler.
- **Wave cadence:** one wave is spawned per beat event.
- **Per-wave rendering without heap allocations:** waves render directly into the LED buffer (no per-wave vector allocations).
- **Wave spacing throttled:** spacing is applied only on changes or every ~`WAVE_SPACING_INTERVAL_MS` instead of every frame.
- **Wave cleanup:** waves are dropped if they produce no LED > 1 while inside the active frame range.
- **Forward-only waves:** random reverse is removed; waves start outside the pattern with the nose leading.
- **Configurable hue drift:** each wave starts at base hue and drifts to a random end offset controlled by `WAVE_HUE_DRIFT_ROUNDS`.
- **Pulse tuning:** pulse min ratio and decay time are now configurable and driven by **real** beats only.
- **FFT size reduced:** `AUDIO_FFT_SAMPLES` is now `256` for lower CPU load.
- **Bass envelope (FFT only):** time-domain envelope is disabled by default (`BASS_ENVELOPE_TIME_DOMAIN=0`).
- **Audio task:** audio processing runs in a dedicated FreeRTOS task (`AUDIO_TASK_ENABLE=1` in `platformio.ini` `build_flags`).

## How It Works (Detailed)

This section documents the full audio -> beat -> wave -> LED pipeline.
All formulas and constants below reflect the current code.

### Audio Capture + FFT
**Where:** `main/audio_processor.cpp`

1) **I2S read (mono) + shift**  
Samples are read as 32-bit mono. The SPH0645 data is shifted into a signed
24-bit range:

```
sample = (raw >> SPH0645_RAW_SHIFT)
```

2) **DC removal + Hann window**
```
mean = sum(sample) / N
centered = sample - mean
windowed = centered * hann[i]
```

3) **FFT bin metrics (bass + flux)**
```
binWidthHz = sampleRateHz / fftSamples
binMin = floor(bassMinHz * fftSamples / sampleRateHz)
binMax = floor(bassMaxHz * fftSamples / sampleRateHz)

mag[b] = sqrt(re[b]^2 + im[b]^2)
bass = sum_{b=binMin..binMax} mag[b]
flux = sum_{b=binMin..binMax} max(0, mag[b] - prevMag[b])
```

### Beat Detection (Spectral Flux)
**Where:** `main/audio_processor.cpp`

The detector uses EMAs for a moving baseline and then looks for a rising flux event:

```
bassEma = (1 - energyEmaAlpha) * bassEma + energyEmaAlpha * bass
fluxEma = (1 - fluxEmaAlpha) * fluxEma + fluxEmaAlpha * flux

intervalOk = (now - lastBeatMs) >= minBeatIntervalMs
above = flux > fluxEma * fluxThreshold
rise = flux - prevFlux
rising = rise > fluxEma * fluxRiseFactor

beat = intervalOk && above && rising
```

Beat strength is derived from the flux ratio:

```
ratio = flux / (fluxEma + 1e-3)
strength = clamp01((ratio - fluxThreshold) / fluxThreshold)
```

If I2S init fails, a lightweight fake beat generator feeds the animation engine
during the initial **fake-beat window** (`FADE_TO_STARTUP_IDLE_MS` after the last
real beat). After that, relaxation ticks keep waves moving.

### Tempo Estimate (Average Beat Interval)
**Where:** `main/audio_processor.cpp`

A rolling buffer (N=6) of recent beat intervals is median-filtered and then smoothed:

```
intervalMs = clamp(now - lastBeatMs, avgBeatMinMs, avgBeatMaxMs)
median = median(last N intervals)

avgBeatIntervalMs =
  (1 - kBeatIntervalEmaAlpha) * avgBeatIntervalMs
  + kBeatIntervalEmaAlpha * median
```

### Wave Scheduling
**Where:** `main/animation_engine.cpp`

Waves are **event-driven**. A wave can be spawned on three kinds of beat events:

1) **Real beat detected** (FFT flux trigger)  
2) **Fake beat** (only during the first `FADE_TO_STARTUP_IDLE_MS` after the last real beat)  
3) **Relaxation tick** (during fade-to-startup, a periodic tick keeps waves moving)

Beat timing uses the last interval if available, otherwise the averaged interval, then clamps it:

```
beatPeriodMs = getAverageBeatIntervalMs()
effectiveIntervalMs = (lastBeatIntervalMs > 0) ? lastBeatIntervalMs : beatPeriodMs
beatPeriodMs = clamp(effectiveIntervalMs, beatDecayMinMs, beatDecayMaxMs)
```

**Fake beat window (`FADE_TO_STARTUP_IDLE_MS`)**  
If no real beat is detected but you are still inside that fake-beat window,
a synthetic beat is emitted every `intervalMs` (derived from the last or averaged interval).

**Relaxation ticks (after fake-beat window)**  
Once fade-to-startup begins, a periodic tick fires every `beatPeriodMs` to
continue emitting waves even without beat detections and update the synthetic beat interval.
Fade completes after `FADE_TO_STARTUP_DURATION_MS` (20s by default).

**Wave cadence**

Waves are spawned **exactly on beat events** (real beat, fake beat in the fake-beat
window, or relaxation tick during fade-to-startup):

```
if beatEvent:
  spawn wave
```

When spawning, the engine respects `maxActiveWaves` and drops the oldest wave if needed.

### Fallback Waves (No Beat Waves)
**Where:** `main/animation_engine.cpp`

If beat-driven waves are disabled but fallback is enabled:

```
if !enableBeatWaves && enableFallbackWaves
  and (now - lastBeatMs >= fallbackMs)
  and (now - lastWaveTime >= fallbackMs):
    spawn wave with strength = 0
```

### Wave Speed
**Where:** `main/animation_engine.cpp`, `main/wave_position.cpp`

1) **Beat period used for waves**  
Each wave uses the **current effective beat interval** (last interval if available,
otherwise the averaged interval), clamped by:

```
beatPeriodMs = clamp(effectiveIntervalMs, beatDecayMinMs, beatDecayMaxMs)
```

2) **Period -> speed mapping**  
Speed is derived per wave using a linear BPM → speed-control mapping:

```
bpm = 60000 / beatPeriodMs
bpmMin = 60000 / avgBeatMaxMs   // lowest BPM allowed
bpmMax = 60000 / avgBeatMinMs   // highest BPM allowed

t = clamp((bpm - bpmMin) / (bpmMax - bpmMin), 0..1)
speedControl = round(-10 + t * 20)    // -10..+10
```

Then in `addWave()`:

```
ctl = speedControl / 10.0         // -1..+1
range = clamp(waveSpeedRange, 0..1)
if ctl >= 0:
  speedScale = 1 + range * ctl
else:
  speedScale = 1 / (1 + range * -ctl)

speed = waveSpeedBase * speedScale
speedPerSec = speed * waveSpeedBaseFps * waveSpeedMultiplier
```

`waveSpeedRange = 0` gives no variation.  
`waveSpeedRange = 1` gives `0.5x .. 2x` around the base speed.

3) **Position update (time-based)**  
Wave motion is now time-based:
```
dt = (nowMs - lastUpdateMs) / 1000
wave.center += wave.speed * dt
```

`wave.speed` is stored in **frames per second**, derived from `waveSpeedBase` and the
multiplier mapping above, then scaled by a base FPS of `WAVE_SPEED_BASE_FPS`
(set in `app_main()` via `setWaveSpeedBaseFps()`).

---

### Wave Width (Ratios)
**Where:** `main/animation_engine.cpp`

Wave width is derived from the **spacing between wave peaks**. That spacing is
split from `nose + gap + nose` (same nose ratio on both sides):

```
spacing = distance between wave centers
total = 2 * WAVE_NOSE_RATIO + WAVE_GAP_RATIO
nose = spacing * (WAVE_NOSE_RATIO / total)
gap  = spacing * (WAVE_GAP_RATIO / total)
tail = spacing * (WAVE_NOSE_RATIO / total)
```

The `nose` is the *leading* side and `tail` is the *trailing* side of the wave
in animation-frame space (not LED indices). Ratios are clamped so the tail is never
negative.

**Visual example (`WAVE_NOSE_RATIO=0.2`, `WAVE_GAP_RATIO=0.2`):**
```
peak A  |<--------- spacing between peaks --------->|  peak B
        [ tail 33% ][ gap 33% ][ nose 33% ]
```

At spawn time, the spacing is estimated from the effective beat period:

```
spacing = (|speed| * (beatPeriodMs / 1000)) / (waveSpeedBase * waveSpeedMultiplier)
```

When multiple waves are active, spacing updates recompute **nose** and **tail**
using the **actual distance between adjacent wave centers**, so the ratios stay
consistent even as waves drift.

---

### Wave Start Position
**Where:** `main/wave_position.cpp`

Waves always move forward (no reverse). The starting center is placed just
outside the pattern with a fixed margin:

```
startCenter = -1.0
```

Frames outside `0..maxIndex` do not light any LEDs.

### Wave Color
**Where:** `main/animation_engine.cpp`, `main/wave_position.cpp`, `main/frame_interpolation.cpp`

1) **Base hue per wave**  
At spawn time:
```
baseHue = random(0, 65536)   // 0..65535
```

2) **Hue sweep per wave**  
Each wave starts at base hue (`hueStartOffset = 0`) and gets a random end offset:
```
rounds = abs(WAVE_HUE_DRIFT_ROUNDS)
maxOffset = rounds * 65535
hueEndOffset = random(-maxOffset, maxOffset)
```

During the wave's lifetime:
```
progress = clamp((center - startCenter) / (endCenter - startCenter), 0..1)
offset   = lerp(hueStartOffset, hueEndOffset, progress)
hue      = (baseHue + offset) mod 65536
```

This allows bidirectional hue drift over the wave lifetime.
Examples: `WAVE_HUE_DRIFT_ROUNDS=0` => no drift, `1` => up to +/-1 full turn.

3) **Final pixel color**  
For each affected LED:
```
color = HSV(hue, 255, brightness)
```

---

### Wave Envelope (Spatial Intensity)
**Where:** `main/waveform.cpp`, `main/frame_interpolation.cpp`

The spatial envelope uses an **asymmetric smoothstep** around the wave center:

```
d = frameIndex - center
w = (d < 0) ? tailWidth : noseWidth
x = |d| / w

if x >= 1: intensity = 0
else:
  t = 1 - x
  intensity = t^2 * (3 - 2*t)
```

This gives a smooth, bell-like falloff with different widths in front (nose)
and behind (tail).

---

### Global Brightness + Pulse Envelope
**Where:** `main/animation_engine.cpp`

The base frame brightness is a **global multiplier** applied on top of the wave
intensity (nose/tail envelope). A separate **pulse envelope** is then applied to
every LED after rendering, right before `strip.show()`.

Behavior (relative to the `brightness` setting):
- Base brightness starts at **70%**.
- Once a **real beat** has been detected, base brightness is **100%**.
- Pulse is driven **only by real beats** (fake beats do not restart the pulse).
- `pulseLeadMs` shifts the pulse forward in time to compensate for detection latency
  (positive values advance the pulse, negative values delay it).
- During fade-to-startup, base brightness is lerped back toward **70%** and the
  pulse is lerped toward **1.0** (disabled).

Formulas (simplified):

```
baseBrightnessRatio = 0.70
pulseRatio = 1.0

if (lastRealBeatMs > 0) {
  baseBrightnessRatio = 1.0
  intervalMs = (lastRealBeatIntervalMs > 0)
               ? lastRealBeatIntervalMs
               : smoothedBeatPeriodMs

  intervalMs *= beatPulseDecayRatio
  e = 1.0 - ((pulseNow - lastRealBeatMs) / intervalMs)   // 1..0
  if BEAT_DECAY_EASE_OUT: e = e * e
  pulseRatio = beatPulseMinRatio + (1 - beatPulseMinRatio) * e
  pulseRatio = clamp(pulseRatio, beatPulseMinRatio, 1.0)
}

frameBrightness = g_config.brightness * baseBrightnessRatio
rgb = rgb * pulseRatio
```

---

### Animation Switching (Auto Mode)
**Where:** `main/animation_manager.cpp`

Auto mode switches animations based on BPM changes:

```
if autoMode and bpm > 0 and (now - bpmWindowStart) >= BPM_SWITCH_WINDOW_MS:
  diff = abs(bpm - bpmWindowStartBpm) / bpmWindowStartBpm
  if diff >= BPM_SWITCH_THRESHOLD:
    switch to random next animation
```

If BPM is unavailable, a fixed time-based fallback is used:

```
if autoMode and (now - lastSwitchTime) >= AUTO_SWITCH_INTERVAL_MS:
  switch to random next animation
```

---

### Wave Spacing Updates
**Where:** `main/animation_engine.cpp`

Spacing updates run on changes or every `WAVE_SPACING_INTERVAL_MS`. The engine
measures **actual distance between adjacent wave centers** and recomputes:

```
total = 2 * WAVE_NOSE_RATIO + WAVE_GAP_RATIO
nose = spacing * (WAVE_NOSE_RATIO / total)
tail = spacing * (WAVE_NOSE_RATIO / total)
```

This keeps the nose/gap/tail proportions stable regardless of wave speed.

---

### Wave Lifetime (Removal)
**Where:** `main/wave_position.cpp`, `main/animation_engine.cpp`

Forward waves are removed when:
```
center > maxIndex + noseWidth + 1
```

Additionally, a wave is removed if it produces **no LED > 1** while its center
is inside the active frame range (0..maxIndex). This avoids waves lingering
with no visible contribution.

---

- **Auto-mode BPM switching:** in auto mode, animation switches when BPM changes by
  at least `BPM_SWITCH_THRESHOLD` over `BPM_SWITCH_WINDOW_MS`; otherwise it falls
  back to `AUTO_SWITCH_INTERVAL_MS`.
