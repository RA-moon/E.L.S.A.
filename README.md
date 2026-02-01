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
