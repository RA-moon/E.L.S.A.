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
#define AUDIO_FFT_SAMPLES 512
```
The firmware will try the primary sample rate and fall back if init fails.

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

## Wave/Beat/Color Behavior (Detailed)

This section documents how the current firmware computes:
`wave speed`, `wave width`, `color`, and the `wave envelope`.
All formulas and constants below reflect the current code.

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

Then the wave period is clamped to the **hard BPM window** (75–140 BPM):

```
periodMs = clamp(smoothedBeatPeriodMs, avgBeatMinMs, avgBeatMaxMs)
```

Current defaults (hard clamped in `normalizeConfig()`):
- `avgBeatMinMs = 430` (≈ 140 BPM max)
- `avgBeatMaxMs = 800` (≈ 75 BPM min)

2) **Period → speed mapping**  
Speed is derived *per wave* at spawn time using:

```
t = clamp((bpm - bpmMin) / (bpmMax - bpmMin), 0..1)
speed = speedMin + t * (speedMax - speedMin)
bpmMin = 74
bpmMax = 130
speedMin = 0.1
speedMax = 0.4
```

Then it is converted to an internal control value:

```
speedControl = round((speed - 0.2) * 25)
```

And finally converted back in `addWave()`:

```
speed = 0.2 + (speedControl / 25)
speed = clamp(speed, 0.1..0.6)
```

3) **Position update (time‑based)**  
Wave motion is now time‑based:
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
in animation‑frame space (not LED indices).

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

These are converted to 16‑bit hue offsets:
```
offset = round(deg * 65535 / 360)
```

During the wave’s lifetime:
```
progress = clamp((center - startCenter) / (endCenter - startCenter), 0..1)
offset   = lerp(hueStartOffset, hueEndOffset, progress)
hue      = (baseHue + offset) mod 65536
```

This allows **bidirectional hue rotation**, up to ±2 full rotations between
start and end offsets.

3) **Final pixel color**  
For each affected LED:
```
color = HSV(hue, 255, brightness)
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

This gives a smooth, bell‑like falloff with different widths in front (nose)
and behind (tail).

---

### Optional: Continuous Wave Spacing
**Where:** `src/wave_position.cpp`

Waves moving in the **same direction** are continuously spaced by adjusting
the follower’s **nose width**:

```
distance   = leader.center - follower.center
targetNose = distance - leader.tailWidth
follower.nose = (1 - mix) * follower.nose + mix * targetNose
```

Spacing is applied on changes or every `WAVE_SPACING_INTERVAL_MS`.

Current mix: `WAVE_SPACING_MIX = 0.35`  
Nose limits: `WAVE_NOSE_MIN = 0.2`, `WAVE_NOSE_MAX = 2.0`

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

If you want any of these parameters moved into `/config` or exposed in
telemetry, say the word.
 - **Auto-mode BPM switching:** in auto mode, the animation switches when BPM changes by ≥5% (min interval 3s). If BPM is unavailable, it falls back to 10s switching.
