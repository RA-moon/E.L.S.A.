# E.L.S.A. (ESP32-S3 LED + Audio Beat Visualizer)

ESP32-S3 project for driving addressable LEDs with audio-reactive animations and a small web UI for telemetry/control.

## Features
- Audio beat detection (SPH0645 I2S mic)
- Multiple animation patterns (auto or fixed)
- Web UI telemetry + live config controls
- PlatformIO build + upload

## Hardware (current wiring)
- Board: ESP32-S3 Super Mini (4MB Flash / 2MB PSRAM)
- LED data: GPIO1
- I2S mic (SPH0645):
  - BCLK: GPIO5
  - LRCLK/WS: GPIO6
  - DOUT (mic) -> DIN: GPIO7
  - SEL -> GND (left channel)
  - Pins default to `I2S_BCLK_PIN=5`, `I2S_WS_PIN=6`, `I2S_DIN_PIN=7` in `src/audio_processor.cpp`.
    To override, set `-DI2S_BCLK_PIN=...`, `-DI2S_WS_PIN=...`, `-DI2S_DIN_PIN=...` in `platformio.ini`.

## Build + Upload (PlatformIO)
```bash
pio run -e esp32-s3-super-mini
pio run -e esp32-s3-super-mini -t upload
```

If upload is flaky, hold BOOT, tap RESET, then release BOOT after "Connecting..." appears.

## Wi-Fi
Set credentials in `include/wifi_secrets.h`:
```c
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"
```
ESP32-S3 supports **2.4 GHz only**.

## Web UI
When Wi-Fi connects, Serial prints:
```
WiFi connected: <ip>
Web telemetry ready: http://<ip>/
```
Endpoints:
- `/` UI
- `/status` JSON
- `/frame` raw LED frame (3 bytes/LED)
- `/config` get/set runtime config

Example:
```
http://<ip>/config?mode=fixed&anim=1&brightness=80&beatMin=160&beatMax=1500&fallbackMs=800&maxWaves=20&beatWaves=1&fallbackWaves=1
```

## Audio tuning
Shared defaults live in `include/audio_config.h`:
```c
#define AUDIO_SAMPLE_RATE_HZ 32000
#define AUDIO_FFT_SAMPLES 256
```
The firmware will try the primary sample rate and fall back if init fails.
Note: On ESP32-S3, `audio_config.h` sets the defaults above; on other targets,
`src/audio_processor.cpp` defaults to `AUDIO_FFT_SAMPLES=512` unless overridden.

## Notes
- `strip.show()` (NeoPixel) is the largest CPU cost.
- Close the web UI if you want to reduce CPU/network load.

## Recent Performance/Behavior Changes
- **Time-based wave motion:** wave position now advances by `speed * dt` (seconds), so speed is stable even if FPS jitters.
- **Wave speed base FPS:** base FPS is set to `1000 / DELAY_MS` in `setup()` to preserve legacy speed scaling.
- **Per-wave rendering without heap allocations:** waves render directly into the LED buffer (no per-wave vector allocations).
- **Wave spacing throttled:** spacing is applied only on changes or every ~`WAVE_SPACING_INTERVAL_MS` instead of every frame.
- **FFT size reduced:** `AUDIO_FFT_SAMPLES` is now `256` for lower CPU load.
- **Bass envelope (FFT only):** time-domain envelope is disabled by default (`BASS_ENVELOPE_TIME_DOMAIN=0`).
- **Web telemetry off by default:** set `ENABLE_WEB_TELEMETRY 1` in `src/main.cpp` to re-enable.
- **`/config` disabled by default:** set `ENABLE_CONFIG_ENDPOINT 1` in `src/main.cpp` to enable it.
- **Audio task:** audio processing runs in a dedicated FreeRTOS task (`AUDIO_TASK_ENABLE=1`).

## How It Works (Detailed)

This section documents the full audio -> beat -> wave -> LED pipeline.
All formulas and constants below reflect the current code.

### Audio Capture + FFT
**Where:** `src/audio_processor.cpp`

1) **I2S read + channel select**  
Samples are read as interleaved 32-bit stereo. The SPH0645 channel is selected
and shifted into a signed 24-bit range:

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
**Where:** `src/audio_processor.cpp`

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

If I2S init fails, a lightweight fake beat generator is used so animations still run.

### Tempo Estimate (Average Beat Interval)
**Where:** `src/audio_processor.cpp`

A rolling buffer (N=6) of recent beat intervals is median-filtered and then smoothed:

```
intervalMs = clamp(now - lastBeatMs, avgBeatMinMs, avgBeatMaxMs)
median = median(last N intervals)

avgBeatIntervalMs =
  (1 - kBeatIntervalEmaAlpha) * avgBeatIntervalMs
  + kBeatIntervalEmaAlpha * median
```

### Wave Scheduling
**Where:** `src/main.cpp`

The beat period is smoothed and clamped to the BPM window before scheduling waves:

```
beatPeriodMs = clamp(getAverageBeatIntervalMs(), beatDecayMinMs, beatDecayMaxMs)

smoothedBeatPeriodMs =
  (1 - BEAT_PERIOD_EMA_ALPHA) * smoothedBeatPeriodMs
  + BEAT_PERIOD_EMA_ALPHA * beatPeriodMs

periodMs = clamp(smoothedBeatPeriodMs, avgBeatMinMs, avgBeatMaxMs)
```

Waves are scheduled against a moving deadline:

```
if nextWaveDueMs == 0: nextWaveDueMs = now + periodMs
if periodMs changed:
  nextWaveDueMs = (lastWaveTime > 0) ? lastWaveTime + periodMs : now + periodMs
if now >= nextWaveDueMs:
  spawn wave
  nextWaveDueMs += periodMs  // repeat to catch up
```

When spawning, the engine respects `maxActiveWaves` and drops the oldest wave if needed.

### Fallback Waves (No Beat Waves)
**Where:** `src/main.cpp`

If beat-driven waves are disabled but fallback is enabled:

```
if !enableBeatWaves && enableFallbackWaves
  and (now - lastBeatMs >= fallbackMs)
  and (now - lastWaveTime >= fallbackMs):
    spawn wave with strength = 0
```

### Wave Speed
**Where:** `src/main.cpp`, `src/wave_position.cpp`

1) **Beat period used for waves**  
The wave scheduler uses a *smoothed* beat period:

```
beatPeriodMs = clamp(getAverageBeatIntervalMs(),
                     beatDecayMinMs, beatDecayMaxMs)

smoothedBeatPeriodMs =
  (1 - BEAT_PERIOD_EMA_ALPHA) * smoothedBeatPeriodMs
  + BEAT_PERIOD_EMA_ALPHA * beatPeriodMs
```

Then the wave period is clamped to the **hard BPM window** (75-140 BPM):

```
periodMs = clamp(smoothedBeatPeriodMs, avgBeatMinMs, avgBeatMaxMs)
```

Current defaults (hard clamped in `normalizeConfig()`):
- `avgBeatMinMs = 430` (approx 140 BPM max)
- `avgBeatMaxMs = 800` (approx 75 BPM min)

2) **Period -> speed mapping**  
Speed is derived *per wave* at spawn time using:

```
t = clamp((bpm - bpmMin) / (bpmMax - bpmMin), 0..1)
speed = speedMin + t * (speedMax - speedMin)
bpmMin = 74
bpmMax = 130
speedMin = 0.05
speedMax = 0.15
```

Then it is converted to an internal control value:

```
speedControl = round((speed - 0.2) * 25)
```

And finally converted back in `addWave()`:

```
speed = 0.2 + (speedControl / 25)
speed = clamp(speed, 0.1..0.6)
speedPerSec = speed * waveSpeedBaseFps
```

3) **Position update (time-based)**  
Wave motion is now time-based:
```
dt = (nowMs - lastUpdateMs) / 1000
wave.center += wave.speed * dt
```

`wave.speed` is stored in **frames per second**, derived from the original 0.2..0.6 scale
by using a base FPS of `1000 / DELAY_MS` (set in `setup()` via `setWaveSpeedBaseFps()`).

---

### Wave Width (ASRD)
**Where:** `src/main.cpp`

Wave width is built from **Attack/Sustain/Release/Decay** (ASRD) values that
interpolate between a **minimum** width (sum=1.0) and **maximum** width (sum=4.0)
based on beat strength (`0..1`):

```
attack  = lerp(WAVE_ATTACK_MIN,  WAVE_ATTACK_MAX,  strength)
sustain = lerp(WAVE_SUSTAIN_MIN, WAVE_SUSTAIN_MAX, strength)
release = lerp(WAVE_RELEASE_MIN, WAVE_RELEASE_MAX, strength)
decay   = lerp(WAVE_DECAY_MIN,   WAVE_DECAY_MAX,   strength)
```

Current constants:
- Min: `attack=0.2`, `sustain=0.3`, `release=0.3`, `decay=0.2` (sum=1.0)
- Max: `attack=0.8`, `sustain=1.2`, `release=1.2`, `decay=0.8` (sum=4.0)

These are mapped to **nose/tail**:

```
nose = attack + decay
tail = sustain + release
```

The `nose` is the *leading* side and `tail` is the *trailing* side of the wave
in animation-frame space (not LED indices).

At spawn time, `nose` is clamped to `[WAVE_NOSE_MIN, WAVE_NOSE_MAX]`.

---

### Wave Color
**Where:** `src/main.cpp`, `src/wave_position.cpp`, `src/frame_interpolation.cpp`

1) **Base hue per wave**  
At spawn time:
```
baseHue = random(0, 65536)   // 0..65535
```

2) **Hue sweep per wave**  
Each wave also gets a random **start** and **end** offset in degrees:
```
hueStartDeg = random(-360, 361)
hueEndDeg   = random(-360, 361)
```

These are converted to 16-bit hue offsets:
```
offset = round(deg * 65535 / 360)
```

During the wave's lifetime:
```
progress = clamp((center - startCenter) / (endCenter - startCenter), 0..1)
offset   = lerp(hueStartOffset, hueEndOffset, progress)
hue      = (baseHue + offset) mod 65536
```

This allows **bidirectional hue rotation**, up to +/-2 full rotations between
start and end offsets.

3) **Final pixel color**  
For each affected LED:
```
color = HSV(hue, 255, brightness)
```

Reverse waves are randomly chosen at spawn time:
```
reverse = (random(0, 100) < 25)
```

---

### Wave Envelope (Spatial Intensity)
**Where:** `src/waveform.cpp`, `src/frame_interpolation.cpp`

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
**Where:** `src/main.cpp`

The base frame brightness is a **global multiplier** applied on top of the wave
intensity (nose/tail envelope). A separate **pulse envelope** is then applied to
every LED after rendering, right before `strip.show()`.

Behavior (relative to the `brightness` setting):
- If no valid BPM is detected, base brightness stays at **70% of brightness**.
- If BPM is valid and recent, base brightness is **100% of brightness** and the
  pulse envelope eases down to **30%** between beats.
- `pulseLeadMs` shifts the pulse forward in time to compensate for detection latency
  (positive values advance the pulse, negative values delay it).

Formulas:

```
bpmInRange =
  (lastBeatIntervalMs >= avgBeatMinMs) &&
  (lastBeatIntervalMs <= avgBeatMaxMs)

beatRecent =
  (lastBeatMs > 0) &&
  ((now - lastBeatMs) <= (avgBeatMaxMs * 2))

baseBrightnessRatio = 0.70
pulseRatio = 1.0
pulseNow = now + pulseLeadMs

if (bpmInRange && beatRecent) {
  intervalMs = (lastBeatIntervalMs > 0) ? lastBeatIntervalMs : smoothedBeatPeriodMs
  intervalMs = clamp(intervalMs, avgBeatMinMs, avgBeatMaxMs)

  baseBrightnessRatio = 1.0
  e = 1.0 - ((pulseNow - lastBeatMs) / intervalMs)   // 1..0
  if BEAT_DECAY_EASE_OUT: e = e * e
  pulseRatio = BRIGHTNESS_MIN_RATIO + (BRIGHTNESS_MAX_RATIO - BRIGHTNESS_MIN_RATIO) * e
}

frameBrightness = g_config.brightness * baseBrightnessRatio

// After rendering:
rgb = rgb * pulseRatio
```

---

### Animation Switching (Auto Mode)
**Where:** `src/animation_manager.cpp`

Auto mode switches animations based on BPM changes, with a minimum time between switches:

```
diff = abs(bpm - lastSwitchBpm) / lastSwitchBpm
if autoMode and bpm > 0 and diff >= 0.05 and (now - lastSwitchTime) >= 3000:
  switch to next animation
```

If BPM is unavailable, a fixed time-based fallback is used:

```
if autoMode and bpm <= 0 and (now - lastSwitchTime) >= 10000:
  switch to next animation
```

---

### Button Control
**Where:** `src/main.cpp`

The button uses debounce + a double-tap window:

```
debounce = 30ms
doubleTapWindow = 350ms

first tap -> wait for second
second tap within window -> toggle auto mode
if window expires and auto mode is off -> advance animation index
```

---

### Optional: Continuous Wave Spacing
**Where:** `src/wave_position.cpp`

Waves moving in the **same direction** are continuously spaced by adjusting
the follower's **nose width**:

```
distance   = leader.center - follower.center
targetNose = distance - leader.tailWidth
follower.nose = (1 - mix) * follower.nose + mix * targetNose
```

Spacing is applied on changes or every `WAVE_SPACING_INTERVAL_MS`.

Current mix: `WAVE_SPACING_MIX = 0.35`  
Nose limits: `WAVE_NOSE_MIN = 0.2`, `WAVE_NOSE_MAX = 3.0`

---

### Wave Lifetime (Removal)
**Where:** `src/wave_position.cpp`

Forward waves are removed when:
```
center > maxIndex + noseWidth + 1
```

Reverse waves are removed when:
```
center < -tailWidth - 1
```

---

- **Auto-mode BPM switching:** in auto mode, the animation switches when BPM changes by >=5% (min interval 3s). If BPM is unavailable, it falls back to 10s switching.
