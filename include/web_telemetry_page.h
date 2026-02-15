#pragma once

#include <Arduino.h>

static const char kWebTelemetryHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>ESP32 Beat + Frame Telemetry</title>
    <style>
      body { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; background: #0f1217; color: #e8eef7; margin: 24px; }
      h1 { font-size: 20px; margin-bottom: 8px; }
      pre { background: #151a22; padding: 16px; border-radius: 10px; }
      .muted { color: #8b96a8; font-size: 12px; }
      #strip { width: 100%; height: 60px; image-rendering: pixelated; border-radius: 8px; background: #0b0e13; }
      .row { display: flex; gap: 12px; align-items: center; margin: 12px 0; }
      form { background: #151a22; padding: 16px; border-radius: 10px; margin-top: 16px; }
      label { display: flex; gap: 12px; align-items: center; justify-content: space-between; flex: 1; }
      input[type="number"], select { background: #0b0e13; color: #e8eef7; border: 1px solid #2a3342; border-radius: 6px; padding: 4px 8px; }
      input[type="range"] { width: 180px; }
      button { background: #2b6cb0; color: #ffffff; border: none; border-radius: 8px; padding: 8px 12px; cursor: pointer; }
      button:disabled { opacity: 0.6; cursor: default; }
      .small { color: #8b96a8; font-size: 12px; }
      .col { display: flex; flex-direction: column; gap: 4px; flex: 1; }
    </style>
  </head>
  <body>
    <h1>ESP32 Beat + Frame Telemetry</h1>
    <div class="row">
      <canvas id="strip" width="120" height="1"></canvas>
      <div class="small">/frame feed</div>
    </div>
    <pre id="status">loading...</pre>
    <form id="cfg" style="display:none">
      <div class="row">
        <div class="col">
          <label>Brightness <span id="brightnessValue"></span></label>
          <input type="range" id="brightness" min="0" max="255">
        </div>
        <div class="col">
          <label>Pulse lead (ms)</label>
          <input type="number" id="pulseLeadMs" step="1">
        </div>
      </div>
      <div class="row">
        <div class="col">
          <label>Beat decay min (ms)</label>
          <input type="number" id="beatMin" step="10">
        </div>
        <div class="col">
          <label>Beat decay max (ms)</label>
          <input type="number" id="beatMax" step="10">
        </div>
        <div class="col">
          <label>Fallback waves (ms)</label>
          <input type="number" id="fallbackMs" step="10">
        </div>
      </div>
      <div class="row">
        <div class="col">
          <label>Max waves</label>
          <input type="number" id="maxWaves" step="1">
        </div>
        <div class="col">
          <label>Beat waves</label>
          <select id="beatWaves">
            <option value="1">on</option>
            <option value="0">off</option>
          </select>
        </div>
        <div class="col">
          <label>Fallback waves</label>
          <select id="fallbackWaves">
            <option value="1">on</option>
            <option value="0">off</option>
          </select>
        </div>
      </div>
      <div class="row">
        <div class="col">
          <label>Energy EMA</label>
          <input type="number" id="energyEmaAlpha" step="0.01">
        </div>
        <div class="col">
          <label>Flux EMA</label>
          <input type="number" id="fluxEmaAlpha" step="0.01">
        </div>
        <div class="col">
          <label>Flux threshold</label>
          <input type="number" id="fluxThreshold" step="0.01">
        </div>
        <div class="col">
          <label>Flux rise</label>
          <input type="number" id="fluxRiseFactor" step="0.01">
        </div>
      </div>
      <div class="row">
        <div class="col">
          <label>Min beat interval (ms)</label>
          <input type="number" id="minBeatIntervalMs" step="10">
        </div>
        <div class="col">
          <label>Avg beat min (ms)</label>
          <input type="number" id="avgBeatMinMs" step="10">
        </div>
        <div class="col">
          <label>Avg beat max (ms)</label>
          <input type="number" id="avgBeatMaxMs" step="10">
        </div>
      </div>
      <div class="row">
        <div class="col">
          <label>Animation mode</label>
          <select id="mode">
            <option value="auto">auto</option>
            <option value="fixed">fixed</option>
          </select>
        </div>
        <div class="col">
          <label>Animation</label>
          <select id="anim"></select>
        </div>
        <div class="col">
          <button type="submit">Apply</button>
        </div>
      </div>
    </form>
    <script>
      const statusEl = document.getElementById("status");
      const stripEl = document.getElementById("strip");
      const ctx = stripEl.getContext("2d");
      let config = null;
      const fields = ["brightness","beatMin","beatMax","pulseLeadMs","fallbackMs","maxWaves","beatWaves","fallbackWaves","energyEmaAlpha","fluxEmaAlpha","fluxThreshold","fluxRiseFactor","minBeatIntervalMs","avgBeatMinMs","avgBeatMaxMs","mode","anim"];

      async function fetchStatus() {
        const res = await fetch("/status");
        if (!res.ok) return;
        const json = await res.json();
        statusEl.textContent = JSON.stringify(json, null, 2);
      }

      async function fetchFrame() {
        const res = await fetch("/frame");
        if (!res.ok) return;
        if (res.status === 204) return;
        const buf = new Uint8ClampedArray(await res.arrayBuffer());
        const len = Math.floor(buf.length / 3);
        stripEl.width = len;
        for (let i = 0; i < len; i++) {
          ctx.fillStyle = `rgb(${buf[i*3]}, ${buf[i*3+1]}, ${buf[i*3+2]})`;
          ctx.fillRect(i, 0, 1, 1);
        }
      }

      async function fetchConfig() {
        const res = await fetch("/config");
        if (!res.ok) return;
        config = await res.json();
        document.getElementById("cfg").style.display = "block";
        document.getElementById("brightness").value = config.brightness;
        document.getElementById("brightnessValue").textContent = config.brightness;
        document.getElementById("beatMin").value = config.beatDecayMinMs;
        document.getElementById("beatMax").value = config.beatDecayMaxMs;
        document.getElementById("pulseLeadMs").value = config.pulseLeadMs;
        document.getElementById("fallbackMs").value = config.fallbackMs;
        document.getElementById("maxWaves").value = config.maxActiveWaves;
        document.getElementById("beatWaves").value = config.enableBeatWaves ? "1" : "0";
        document.getElementById("fallbackWaves").value = config.enableFallbackWaves ? "1" : "0";
        document.getElementById("energyEmaAlpha").value = config.beat.energyEmaAlpha;
        document.getElementById("fluxEmaAlpha").value = config.beat.fluxEmaAlpha;
        document.getElementById("fluxThreshold").value = config.beat.fluxThreshold;
        document.getElementById("fluxRiseFactor").value = config.beat.fluxRiseFactor;
        document.getElementById("minBeatIntervalMs").value = config.beat.minBeatIntervalMs;
        document.getElementById("avgBeatMinMs").value = config.beat.avgBeatMinMs;
        document.getElementById("avgBeatMaxMs").value = config.beat.avgBeatMaxMs;
        document.getElementById("mode").value = config.animation.mode;
        const animSel = document.getElementById("anim");
        animSel.innerHTML = "";
        config.animations.forEach((name, idx) => {
          const opt = document.createElement("option");
          opt.value = idx;
          opt.textContent = name;
          animSel.appendChild(opt);
        });
        animSel.value = config.animation.index;
      }

      document.getElementById("brightness").addEventListener("input", (ev) => {
        document.getElementById("brightnessValue").textContent = ev.target.value;
      });

      document.getElementById("cfg").addEventListener("submit", async (ev) => {
        ev.preventDefault();
        const data = new URLSearchParams();
        fields.forEach((key) => {
          const el = document.getElementById(key);
          if (!el) return;
          data.set(key, el.value);
        });
        await fetch("/config", { method: "POST", body: data });
        await fetchConfig();
      });

      async function tick() {
        await fetchStatus();
        await fetchFrame();
        requestAnimationFrame(tick);
      }

      fetchConfig();
      tick();
    </script>
  </body>
</html>
)HTML";
