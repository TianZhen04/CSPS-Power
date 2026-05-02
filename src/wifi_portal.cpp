#include <wifi_portal.h>

#include <display.h>
#include <power_ctrl.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

namespace
{
static constexpr const char *kPrefsNs = "wifi_cfg";
static constexpr const char *kDefaultApSsid = "CSPS-Power";
static constexpr const char *kDefaultApPass = "12345678";
static constexpr size_t kMaxSsidLen = 32;
static constexpr size_t kMaxPassLen = 64;
static constexpr int kMaxScanResults = 30;

WebServer g_server(80);
Preferences g_prefs;
pmbus_data_t g_latest_data = {};
pmbus_setup_info_t g_setup_info = {};
c3_sensor_data_t g_c3_data = {};
uint16_t g_shutdown_status = 0;
uint16_t g_warning_status = 0;
String g_shutdown_reason;
String g_warning_reason;

String g_ap_ssid = kDefaultApSsid;
String g_ap_pass = kDefaultApPass;
String g_sta_ssid;
String g_sta_pass;
uint32_t g_wifi_started_ms = 0;
uint32_t g_last_sta_retry_ms = 0;
bool g_wifi_apply_pending = false;
bool g_server_started = false;
uint32_t g_wifi_apply_requested_ms = 0;
uint32_t g_wifi_apply_delay_ms = 0;
bool g_sta_connect_pending = false;
uint32_t g_sta_connect_requested_ms = 0;

bool parse_uint_arg(const String &value, int &out)
{
  if (value.isEmpty())
  {
    return false;
  }

  for (size_t i = 0; i < value.length(); ++i)
  {
    const char c = value.charAt(i);
    if (c < '0' || c > '9')
    {
      return false;
    }
  }

  out = value.toInt();
  return true;
}

String html_page()
{
  return R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>CSPS Power Monitor</title>
  <style>
    :root {
      --bg: #0d1117;
      --surface: #161b22;
      --border: #30363d;
      --text: #c9d1d9;
      --muted: #8b949e;
      --accent: #58a6ff;
      --green: #3fb950;
      --yellow: #d2991d;
      --red: #f85149;
      --cyan: #39d2c0;
      --orange: #f0883e;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }

    body {
      font-family: 'Segoe UI', 'SF Pro Display', 'PingFang SC', 'Microsoft YaHei', sans-serif;
      background: var(--bg);
      color: var(--text);
      padding: 16px;
      min-height: 100vh;
    }

    /* ── Header ── */
    .header {
      display: flex; align-items: center; justify-content: space-between;
      padding: 12px 18px; background: var(--surface);
      border: 1px solid var(--border); border-radius: 10px; margin-bottom: 14px;
    }
    .header h1 { font-size: 20px; font-weight: 600; letter-spacing: 0.5px; }
    .header .status-dot {
      width: 10px; height: 10px; border-radius: 50%; display: inline-block; margin-right: 6px;
    }
    .header .status-dot.green { background: var(--green); box-shadow: 0 0 6px var(--green); }
    .header .status-dot.yellow { background: var(--yellow); box-shadow: 0 0 6px var(--yellow); }
    .header .status-dot.red { background: var(--red); box-shadow: 0 0 6px var(--red); }
    .header .connection { font-size: 13px; color: var(--muted); }

    /* ── Dashboard Grid ── */
    .dashboard {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 12px;
    }

    .card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 16px;
    }
    .card h3 {
      font-size: 13px; font-weight: 500; color: var(--muted);
      text-transform: uppercase; letter-spacing: 0.8px; margin-bottom: 12px;
    }

    /* ── Gauge / Ring ── */
    .gauge-row {
      display: flex; gap: 16px; justify-content: center; flex-wrap: wrap; padding: 8px 0 4px;
    }
    .gauge {
      position: relative; width: 140px; height: 148px;
      display: flex; flex-direction: column; align-items: center; justify-content: center;
    }
    .gauge svg { transform: rotate(-90deg); width: 120px; height: 120px; margin-top: 4px; }
    .gauge .bg-circle { fill: none; stroke: var(--border); stroke-width: 8; }
    .gauge .fg-circle { fill: none; stroke-width: 8; stroke-linecap: round; transition: stroke-dashoffset 0.4s; }
    .gauge .gauge-val {
      position: absolute; font-size: 28px; font-weight: 700; letter-spacing: -0.5px;
    }
    .gauge .gauge-label {
      position: absolute; bottom: -10px; font-size: 15px; color: var(--muted); text-transform: uppercase;
    }

    /* ── Metric Tile ── */
    .metric-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .metric {
      background: var(--bg); border-radius: 8px; padding: 12px 14px;
      border: 1px solid transparent; transition: border-color 0.3s;
    }
    .metric:hover { border-color: var(--border); }
    .metric .value { font-size: 24px; font-weight: 700; letter-spacing: -0.3px; }
    .metric .label { font-size: 11px; color: var(--muted); margin-top: 2px; }
    .metric .sub { font-size: 11px; color: var(--muted); margin-top: 1px; }

    /* ── Colors for value types ── */
    .c-voltage .value { color: var(--accent); }
    .c-current .value { color: var(--yellow); }
    .c-power .value { color: var(--green); }
    .c-temp .value { color: var(--orange); }
    .c-fan .value { color: var(--cyan); }

    /* ── Status Table ── */
    .info-row {
      display: flex; justify-content: space-between; padding: 5px 0;
      font-size: 13px; border-bottom: 1px solid #1c1f26;
    }
    .info-row:last-child { border-bottom: none; }
    .info-row .key { color: var(--muted); }
    .info-row .val { font-weight: 500; }

    /* ── Controls ── */
    input, select {
      width: 100%; padding: 9px 12px; border-radius: 6px;
      border: 1px solid var(--border); background: var(--bg); color: var(--text);
      font-size: 14px; outline: none; transition: border-color 0.2s;
    }
    input:focus, select:focus { border-color: var(--accent); }
    input[type="range"] { padding: 0; height: 6px; accent-color: var(--accent); }
    label { display: block; font-size: 12px; color: var(--muted); margin: 10px 0 4px; }

    button {
      margin-top: 10px; padding: 9px 16px;
      border: 0; border-radius: 6px; font-size: 14px; font-weight: 500;
      cursor: pointer; transition: background 0.2s;
    }
    .btn-primary { background: var(--accent); color: #000; }
    .btn-primary:hover { background: #79b8ff; }
    .btn-secondary { background: var(--border); color: var(--text); }
    .btn-secondary:hover { background: #444c56; }

    .msg { font-size: 12px; margin-top: 6px; min-height: 16px; }
    .msg.ok { color: var(--green); }
    .msg.err { color: var(--red); }

    /* ── Toggle Switch ── */
    .toggle-row {
      display: flex; align-items: center; justify-content: space-between;
      padding: 8px 0; border-bottom: 1px solid #1c1f26;
    }
    .toggle-row:last-child { border-bottom: none; }
    .toggle-row span { font-size: 13px; }
    .toggle-switch {
      position: relative; width: 44px; height: 24px;
    }
    .toggle-switch input { opacity: 0; width: 0; height: 0; }
    .toggle-switch .slider {
      position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;
      background: var(--border); border-radius: 24px; transition: 0.3s;
    }
    .toggle-switch .slider::before {
      content: ""; position: absolute; height: 18px; width: 18px;
      left: 3px; bottom: 3px; background: white; border-radius: 50%; transition: 0.3s;
    }
    .toggle-switch input:checked + .slider { background: var(--accent); }
    .toggle-switch input:checked + .slider::before { transform: translateX(20px); }

    /* ── Wi-Fi Scan List ── */
    .scan-list { margin-top: 8px; max-height: 160px; overflow-y: auto; }
    .net-item {
      display: block; width: 100%; padding: 8px 10px; margin-top: 4px;
      text-align: left; background: var(--bg); border: 1px solid var(--border);
      border-radius: 6px; color: var(--text); cursor: pointer; font-size: 13px;
      transition: border-color 0.2s;
    }
    .net-item:hover { border-color: var(--accent); }

    /* ── Sparkline ── */
    .sparkline { display: block; width: 100%; height: 120px; margin-top: 8px; border-radius: 4px; background: var(--bg); }
    .spark-legend { display: flex; gap: 10px; margin-top: 4px; font-size: 10px; color: var(--muted); }
    .spark-legend span { display: flex; align-items: center; gap: 3px; }
    .spark-legend .dot { width: 6px; height: 6px; border-radius: 50%; display: inline-block; }

    /* ── Fan Control ── */
    .fan-row { display: flex; align-items: center; gap: 10px; }
    .fan-row input { flex: 1; }

    /* ── Alert Banners ── */
    .alert { display: none; padding: 12px 16px; border-radius: 8px; margin-bottom: 12px; font-weight: 500; align-items: center; gap: 10px; }
    .alert.show { display: flex; }
    .alert-danger  { background: rgba(248,81,73,0.15); border: 1px solid var(--red); color: var(--red); }
    .alert-warning { background: rgba(210,153,29,0.12); border: 1px solid var(--yellow); color: var(--yellow); }
    .alert .alert-icon { font-size: 20px; flex-shrink: 0; }
    .alert .alert-body { flex: 1; }
    .alert .alert-title { font-size: 14px; font-weight: 600; margin-bottom: 2px; }
    .alert .alert-detail { font-size: 12px; color: var(--muted); }
    .alert .alert-close { cursor: pointer; font-size: 18px; color: var(--muted); background: none; border: 0; padding: 0 4px; margin: 0; }
    .alert .alert-close:hover { color: var(--text); }

    /* ── Responsive ── */
    @media (max-width: 640px) {
      body { padding: 8px; }
      .dashboard { grid-template-columns: 1fr; }
      .gauge-row { gap: 8px; }
      .gauge { width: 110px; height: 110px; }
      .gauge svg { width: 96px; height: 96px; }
      .gauge .gauge-val { font-size: 22px; }
    }
  </style>
</head>
<body>

  <!-- HEADER -->
  <div class="header">
    <div>
      <h1>&#x26A1; CSPS Power Monitor</h1>
      <div class="connection" id="wifiInfo">AP: -- (--) | STA: -- (--)</div>
    </div>
    <div style="text-align:right;">
      <div style="font-size:13px;color:var(--muted);">Output Status</div>
      <div style="font-size:15px;font-weight:600;" id="outputStatus">
        <span class="status-dot green"></span> ON
      </div>
    </div>
  </div>

  <!-- ALERTS -->
  <div class="alert alert-danger" id="shutdownAlert">
    <span class="alert-icon">&#x26D4;</span>
    <div class="alert-body">
      <div class="alert-title">Power Abnormal Shutdown</div>
      <div class="alert-detail" id="shutdownDetail">-</div>
    </div>
    <button class="alert-close" onclick="return confirm('Dismiss this alert?') && (document.getElementById('shutdownAlert').classList.remove('show'), false);">&#x2715;</button>
  </div>
  <div class="alert alert-warning" id="warningAlert">
    <span class="alert-icon">&#x26A0;</span>
    <div class="alert-body">
      <div class="alert-title">Power Warning</div>
      <div class="alert-detail" id="warningDetail">-</div>
    </div>
  </div>

  <!-- DASHBOARD -->
  <div class="dashboard">

    <!-- INPUT METRICS -->
    <div class="card">
      <h3>Input</h3>
      <div class="metric-grid">
        <div class="metric c-voltage">
          <div class="value" id="vin">0.00</div>
          <div class="label">Voltage (V)</div>
        </div>
        <div class="metric c-current">
          <div class="value" id="iin">0.00</div>
          <div class="label">Current (A)</div>
        </div>
        <div class="metric c-power" style="grid-column:1/-1;">
          <div class="value" id="pin">0.0</div>
          <div class="label">Power (W)</div>
        </div>
      </div>
      <canvas class="sparkline" id="inputSpark"></canvas>
      <div class="spark-legend">
        <span><span class="dot" style="background:var(--accent);"></span> V</span>
        <span><span class="dot" style="background:var(--yellow);"></span> A</span>
        <span><span class="dot" style="background:var(--green);"></span> W</span>
      </div>
    </div>

    <!-- OUTPUT METRICS -->
    <div class="card">
      <h3>Output</h3>
      <div class="metric-grid">
        <div class="metric c-voltage">
          <div class="value" id="vout">0.00</div>
          <div class="label">Voltage (V)</div>
        </div>
        <div class="metric c-current">
          <div class="value" id="iout">0.00</div>
          <div class="label">Current (A)</div>
        </div>
        <div class="metric c-power" style="grid-column:1/-1;">
          <div class="value" id="pout">0.0</div>
          <div class="label">Power (W)</div>
        </div>
      </div>
      <canvas class="sparkline" id="outputSpark"></canvas>
      <div class="spark-legend">
        <span><span class="dot" style="background:var(--accent);"></span> V</span>
        <span><span class="dot" style="background:var(--yellow);"></span> A</span>
        <span><span class="dot" style="background:var(--green);"></span> W</span>
      </div>
    </div>

    <!-- REMOTE SENSOR (C3 via ESP-NOW) -->
    <div class="card">
      <h3>Remote Sensor</h3>
      <div class="metric-grid">
        <div class="metric c-voltage">
          <div class="value" id="c3Vin">--</div>
          <div class="label">Voltage (V)</div>
        </div>
        <div class="metric c-current">
          <div class="value" id="c3Iin">--</div>
          <div class="label">Current (A)</div>
        </div>
        <div class="metric c-power" style="grid-column:1/-1;">
          <div class="value" id="c3Pin">--</div>
          <div class="label">Power (W)</div>
        </div>
      </div>
      <canvas class="sparkline" id="c3Spark"></canvas>
      <div class="spark-legend">
        <span><span class="dot" style="background:var(--accent);"></span> V</span>
        <span><span class="dot" style="background:var(--yellow);"></span> A</span>
        <span><span class="dot" style="background:var(--green);"></span> W</span>
      </div>
      <div style="font-size:11px;color:var(--muted);margin-top:6px;" id="c3Status">
        Waiting for data...
      </div>
    </div>

    <!-- TEMP & FAN GAUGES -->
    <div class="card">
      <h3>Temperature &amp; Cooling</h3>
      <div class="gauge-row">
        <div class="gauge">
          <svg viewBox="0 0 120 120"><circle class="bg-circle" cx="60" cy="60" r="52"/><circle class="fg-circle" id="temp1Gauge" cx="60" cy="60" r="52" stroke="var(--orange)"/></svg>
          <div class="gauge-val" id="temp1" style="color:var(--orange);">0</div>
          <div class="gauge-label">Temp 1 &deg;C</div>
        </div>
        <div class="gauge">
          <svg viewBox="0 0 120 120"><circle class="bg-circle" cx="60" cy="60" r="52"/><circle class="fg-circle" id="temp2Gauge" cx="60" cy="60" r="52" stroke="var(--yellow)"/></svg>
          <div class="gauge-val" id="temp2" style="color:var(--yellow);">0</div>
          <div class="gauge-label">Temp 2 &deg;C</div>
        </div>
        <div class="gauge">
          <svg viewBox="0 0 120 120"><circle class="bg-circle" cx="60" cy="60" r="52"/><circle class="fg-circle" id="fanGauge" cx="60" cy="60" r="52" stroke="var(--cyan)"/></svg>
          <div class="gauge-val" id="fan" style="color:var(--cyan);">0</div>
          <div class="gauge-label">Fan RPM</div>
        </div>
        <div class="gauge">
          <svg viewBox="0 0 120 120"><circle class="bg-circle" cx="60" cy="60" r="52"/><circle class="fg-circle" id="effGauge" cx="60" cy="60" r="52" stroke="var(--green)"/></svg>
          <div class="gauge-val" id="effPct" style="color:var(--green);">0%</div>
          <div class="gauge-label">Efficiency</div>
        </div>
      </div>
    </div>

    <!-- DEVICE INFO -->
    <div class="card">
      <h3>Device Info</h3>
      <div class="info-row"><span class="key">Spare Part No</span><span class="val" id="setupSpn">-</span></div>
      <div class="info-row"><span class="key">Manufacture Date</span><span class="val" id="setupMfg">-</span></div>
      <div class="info-row"><span class="key">Manufacturer</span><span class="val" id="setupMfr">-</span></div>
      <div class="info-row"><span class="key">Power Name</span><span class="val" id="setupName">-</span></div>
      <div class="info-row"><span class="key">Option Kit No</span><span class="val" id="setupOkn">-</span></div>
      <div class="info-row"><span class="key">CT Date Codes</span><span class="val" id="setupCt">-</span></div>
    </div>
  </div>

  <!-- CONTROLS -->
  <div class="dashboard" style="margin-top:12px;">

    <!-- WI-FI -->
    <div class="card">
      <h3>Wi-Fi Settings</h3>
      <label>STA SSID</label><input id="staSsid" type="text" placeholder="Station SSID">
      <label>STA Password</label><input id="staPass" type="password" placeholder="Station password">
      <div style="display:flex;gap:8px;">
        <button class="btn-secondary" onclick="scanWifi()" style="flex:1;">Scan Networks</button>
      </div>
      <div class="scan-list" id="scanList"></div>
      <div class="msg" id="scanMsg"></div>
      <label>AP SSID</label><input id="apSsid" type="text" placeholder="AP SSID">
      <label>AP Password (&ge;8 chars)</label><input id="apPass" type="password" placeholder="AP password">
      <button class="btn-primary" onclick="saveWifi()">Save &amp; Apply Wi-Fi</button>
      <div class="msg" id="wifiMsg"></div>
    </div>

    <!-- DISPLAY & FAN -->
    <div class="card">
      <h3>Display &amp; Fan</h3>
      <label>Brightness</label>
      <div style="display:flex;align-items:center;gap:10px;">
        <input id="brightness" type="range" min="0" max="100" step="1" style="flex:1;">
        <span id="brightnessValue" style="min-width:36px;text-align:right;font-size:14px;">0%</span>
      </div>
      <label>Screen Rotation</label>
      <select id="rotation">
        <option value="0">0&deg; (Normal)</option>
        <option value="1">90&deg;</option>
        <option value="2">180&deg;</option>
        <option value="3">270&deg;</option>
      </select>
      <button class="btn-primary" onclick="saveDisplay()">Apply Display</button>
      <div class="msg" id="displayMsg"></div>
      <hr style="border-color:var(--border);margin:14px 0 10px;">
      <label>Target RPM (1500-17000)</label>
      <div style="display:flex;align-items:center;gap:10px;">
        <input id="fanTarget" type="range" min="1500" max="17000" step="100" style="flex:1;">
        <span id="fanTargetValue" style="min-width:42px;text-align:right;font-size:14px;">0</span>
      </div>
      <button class="btn-primary" onclick="saveFan()">Set Fan Speed</button>
      <div class="msg" id="fanMsg"></div>
    </div>

    <!-- POWER CONTROL -->
    <div class="card">
      <h3>Power Control</h3>
      <div class="toggle-row">
        <span>Software Power</span>
        <label class="toggle-switch"><input id="softwareSwitch" type="checkbox"><span class="slider"></span></label>
      </div>
      <div class="toggle-row">
        <span>Forced Shutdown</span>
        <label class="toggle-switch"><input id="forcedSwitch" type="checkbox"><span class="slider"></span></label>
      </div>
      <div style="font-size:12px;color:var(--muted);margin-top:6px;" id="powerInfo">Output: -- | Force: --</div>
      <button class="btn-primary" onclick="savePower()">Apply Power Control</button>
      <div class="msg" id="powerMsg"></div>
    </div>

  </div>

  <script>
    // ── Pending / Dirty state ──
    let pending = { staSsid: '', apSsid: '', brightness: '', rotation: '', software: null, force: null, fan: '' };

    // ── Helpers ──
    function syncInput(id, serverVal, pendingVal) {
      const el = document.getElementById(id);
      if (!el) return;
      if (document.activeElement === el) return;
      el.value = pendingVal !== '' ? pendingVal : (serverVal || '');
    }
    function syncToggle(id, serverVal, pendingVal) {
      const el = document.getElementById(id);
      if (!el) return;
      if (document.activeElement === el) return;
      el.checked = pendingVal !== null ? pendingVal : !!serverVal;
    }
    function setMsg(id, ok, text) {
      const el = document.getElementById(id);
      if (!el) return;
      el.textContent = text || '';
      el.className = 'msg ' + (ok ? 'ok' : 'err');
    }

    // ── Ring Gauge ──
    function setGauge(id, value, max) {
      const circle = document.getElementById(id);
      if (!circle) return;
      const r = 52, circumference = 2 * Math.PI * r;
      const pct = Math.min(value / max, 1);
      const offset = circumference * (1 - pct);
      circle.style.strokeDasharray = circumference;
      circle.style.strokeDashoffset = offset;
    }

    // ── Auto-sync inputs when user types ──
    document.getElementById('staSsid').addEventListener('input', e => { pending.staSsid = e.target.value; });
    document.getElementById('apSsid').addEventListener('input', e => { pending.apSsid = e.target.value; });
    document.getElementById('brightness').addEventListener('input', e => {
      pending.brightness = e.target.value;
      document.getElementById('brightnessValue').textContent = e.target.value + '%';
    });
    document.getElementById('rotation').addEventListener('change', e => { pending.rotation = e.target.value; });
    document.getElementById('softwareSwitch').addEventListener('change', e => { pending.software = e.target.checked; });
    document.getElementById('forcedSwitch').addEventListener('change', e => { pending.force = e.target.checked; });
    document.getElementById('fanTarget').addEventListener('input', e => {
      pending.fan = e.target.value;
      document.getElementById('fanTargetValue').textContent = e.target.value;
    });

    // ── Ring buffers for sparklines (last 60 samples = 1 minute) ──
    const MAX_SAMPLES = 60;
    let rings = {
      inV: [], inA: [], inW: [],
      outV: [], outA: [], outW: [],
      c3V: [], c3A: [], c3W: []
    };

    function ringPush(arr, val, max) {
      arr.push(val);
      if (arr.length > max) arr.shift();
    }

    function drawSparkline(canvasId, vSeries, aSeries, wSeries) {
      const cvs = document.getElementById(canvasId);
      if (!cvs) return;
      const W = cvs.width = cvs.clientWidth * (devicePixelRatio || 1);
      const h = cvs.clientHeight;
      const H = cvs.height = h * (devicePixelRatio || 1);
      const ctx = cvs.getContext('2d');
      ctx.scale(devicePixelRatio || 1, devicePixelRatio || 1);
      const w = cvs.clientWidth;

      ctx.clearRect(0, 0, w, h);

      const labelW = 42, plotW = w - labelW;
      const gap = 6, bandH = (h - gap * 2) / 3;

      // Bands with gaps: top=V, middle=A, bottom=W
      const bands = [
        { data: vSeries, color: '#58a6ff', y0: 0, y1: bandH, unit: 'V' },
        { data: aSeries, color: '#d2991d', y0: bandH + gap, y1: bandH * 2 + gap, unit: 'A' },
        { data: wSeries, color: '#3fb950', y0: bandH * 2 + gap * 2, y1: h, unit: 'W' }
      ];

      // Separator lines in the gaps between bands
      ctx.strokeStyle = 'rgba(48,54,61,0.6)'; ctx.lineWidth = 1;
      ctx.setLineDash([3, 4]);
      ctx.beginPath(); ctx.moveTo(labelW, bandH + gap / 2); ctx.lineTo(w, bandH + gap / 2); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(labelW, bandH * 2 + gap * 1.5); ctx.lineTo(w, bandH * 2 + gap * 1.5); ctx.stroke();
      ctx.setLineDash([]);

      ctx.font = '11px monospace';
      ctx.textBaseline = 'middle';

      bands.forEach(band => {
        if (band.data.length < 2) return;
        const y0 = band.y0 + 4, y1 = band.y1 - 4;
        let min = Infinity, max = -Infinity;
        for (let i = 0; i < band.data.length; i++) {
          if (band.data[i] < min) min = band.data[i];
          if (band.data[i] > max) max = band.data[i];
        }
        if (max - min < 0.1) { max = min + 0.4; min = min - 0.4; }

        // Range labels on the left
        ctx.fillStyle = band.color;
        ctx.textAlign = 'right';
        ctx.fillText(max.toFixed(1) + band.unit, labelW - 4, y0);
        ctx.fillText(min.toFixed(1) + band.unit, labelW - 4, y1);

        // Draw line
        const range = max - min;
        const stepX = plotW / (MAX_SAMPLES - 1);
        const offsetX = labelW + (MAX_SAMPLES - band.data.length) * stepX;

        ctx.beginPath();
        ctx.strokeStyle = band.color; ctx.lineWidth = 1.2;
        for (let i = 0; i < band.data.length; i++) {
          const x = offsetX + i * stepX;
          const y = y1 - ((band.data[i] - min) / range) * (y1 - y0);
          if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        }
        ctx.stroke();
      });
    }

    // ── API refresh ──
    async function refresh() {
      try {
        const r = await fetch('/api/status');
        const j = await r.json();

        // Threshold-aware helpers
        function fmtCurrent(value, threshold, unit) {
          return value < threshold ? '<' + threshold.toFixed(2) + unit : value.toFixed(2) + unit;
        }
        function fmtPower(value, threshold, unit) {
          return value < threshold ? '<' + threshold.toFixed(0) + unit : value.toFixed(1) + unit;
        }

        // Metrics
        document.getElementById('vin').textContent = j.voltage_in_v.toFixed(2);
        document.getElementById('iin').textContent = fmtCurrent(j.current_in_a, 0.25, '');
        document.getElementById('pin').textContent = fmtPower(j.power_in_w, 20, '');
        document.getElementById('vout').textContent = j.voltage_out_v.toFixed(2);
        document.getElementById('iout').textContent = fmtCurrent(j.current_out_a, 3, '');
        document.getElementById('pout').textContent = fmtPower(j.power_out_w, 36, '');
        document.getElementById('temp1').textContent = j.temp1_c.toFixed(1);
        document.getElementById('temp2').textContent = j.temp2_c.toFixed(1);
        document.getElementById('fan').textContent = Math.round(j.fan_speed_rpm);
        document.getElementById('effPct').textContent = j.efficiency_percent.toFixed(1) + '%';

        // Push to sparkline ring buffers
        ringPush(rings.inV, j.voltage_in_v, MAX_SAMPLES);
        ringPush(rings.inA, j.current_in_a, MAX_SAMPLES);
        ringPush(rings.inW, j.power_in_w, MAX_SAMPLES);
        ringPush(rings.outV, j.voltage_out_v, MAX_SAMPLES);
        ringPush(rings.outA, j.current_out_a, MAX_SAMPLES);
        ringPush(rings.outW, j.power_out_w, MAX_SAMPLES);
        if (j.c3_online) {
          ringPush(rings.c3V, j.c3_voltage_v, MAX_SAMPLES);
          ringPush(rings.c3A, j.c3_current_a, MAX_SAMPLES);
          ringPush(rings.c3W, j.c3_power_w, MAX_SAMPLES);
        }

        // Draw sparklines
        drawSparkline('inputSpark', rings.inV, rings.inA, rings.inW);
        drawSparkline('outputSpark', rings.outV, rings.outA, rings.outW);
        drawSparkline('c3Spark', rings.c3V, rings.c3A, rings.c3W);

        // Gauges
        setGauge('temp1Gauge', j.temp1_c, 120);
        setGauge('temp2Gauge', j.temp2_c, 120);
        setGauge('fanGauge', j.fan_speed_rpm, 17000);
        setGauge('effGauge', j.efficiency_percent, 100);

        // Remote Sensor (C3 via ESP-NOW)
        if (j.c3_online) {
          document.getElementById('c3Vin').textContent = j.c3_voltage_v.toFixed(2);
          document.getElementById('c3Iin').textContent = j.c3_current_a.toFixed(2);
          document.getElementById('c3Pin').textContent = j.c3_power_w.toFixed(1);
          document.getElementById('c3Status').textContent = 'Online | Seq: ' + j.c3_sequence;
        } else {
          document.getElementById('c3Vin').textContent = '--';
          document.getElementById('c3Iin').textContent = '--';
          document.getElementById('c3Pin').textContent = '--';
          document.getElementById('c3Status').textContent = 'Waiting for data...';
        }

        // Device info
        document.getElementById('setupSpn').textContent = j.setup_spn || '-';
        document.getElementById('setupMfg').textContent = j.setup_mfg || '-';
        document.getElementById('setupMfr').textContent = j.setup_mfr || '-';
        document.getElementById('setupName').textContent = j.setup_name || '-';
        document.getElementById('setupOkn').textContent = j.setup_okn || '-';
        document.getElementById('setupCt').textContent = j.setup_ct || '-';

        // Connection
        document.getElementById('wifiInfo').textContent =
          'AP: ' + j.ap_ssid + ' (' + j.ap_ip + ') | STA: ' + (j.sta_ssid || '-') + ' (' + (j.sta_ip || '-') + ')';

        // Output status
        const statusEl = document.getElementById('outputStatus');
        if (j.output_enabled) {
          statusEl.innerHTML = '<span class="status-dot green"></span> ON';
        } else if (j.force_enabled) {
          statusEl.innerHTML = '<span class="status-dot yellow"></span> FORCED';
        } else {
          statusEl.innerHTML = '<span class="status-dot red"></span> OFF';
        }

        // Power info
        document.getElementById('powerInfo').textContent =
          'Output: ' + (j.output_enabled ? 'ON' : 'OFF') +
          ' | Force: ' + (j.force_enabled ? 'Enabled' : 'Forced Off');

        // ── Alerts ──
        const shutdownAlert = document.getElementById('shutdownAlert');
        if (j.shutdown_status && j.shutdown_status !== 0) {
          shutdownAlert.classList.add('show');
          document.getElementById('shutdownDetail').textContent =
            'Reason: ' + (j.shutdown_reason || 'Unknown') + ' (0x' + j.shutdown_status.toString(16).toUpperCase().padStart(4, '0') + ')';
        }

        const warningAlert = document.getElementById('warningAlert');
        if (j.warning_status && j.warning_status !== 0) {
          warningAlert.classList.add('show');
          document.getElementById('warningDetail').textContent =
            'Reason: ' + (j.warning_reason || 'Unknown') + ' (0x' + j.warning_status.toString(16).toUpperCase().padStart(4, '0') + ')';
        } else {
          warningAlert.classList.remove('show');
        }

        // Sync inputs (preserve dirty edits)
        syncInput('staSsid', j.sta_ssid, pending.staSsid);
        syncInput('apSsid', j.ap_ssid, pending.apSsid);
        syncInput('brightness', j.brightness_percent, pending.brightness);
        syncInput('rotation', j.rotation, pending.rotation);
        syncToggle('softwareSwitch', j.software_enabled, pending.software);
        syncToggle('forcedSwitch', j.forced_off, pending.force);
        syncInput('fanTarget', '0', pending.fan);
        document.getElementById('fanTargetValue').textContent =
          (pending.fan !== '' ? pending.fan : '0');

        document.getElementById('brightnessValue').textContent =
          (pending.brightness !== '' ? pending.brightness : j.brightness_percent) + '%';

      } catch (err) {
        console.warn('refresh failed', err);
      }
    }

    // ── Actions ──
    async function saveWifi() {
      const body = new URLSearchParams();
      body.set('staSsid', document.getElementById('staSsid').value);
      body.set('staPass', document.getElementById('staPass').value);
      body.set('apSsid', document.getElementById('apSsid').value);
      body.set('apPass', document.getElementById('apPass').value);
      try {
        const r = await fetch('/api/wifi', { method: 'POST', body });
        const j = await r.json();
        setMsg('wifiMsg', j.ok, j.message);
        if (j.ok) { pending.staSsid = ''; pending.apSsid = ''; }
      } catch (e) { setMsg('wifiMsg', false, 'Request failed'); }
    }

    async function saveDisplay() {
      const body = new URLSearchParams();
      body.set('brightness', document.getElementById('brightness').value);
      body.set('rotation', document.getElementById('rotation').value);
      try {
        const r = await fetch('/api/display', { method: 'POST', body });
        const j = await r.json();
        setMsg('displayMsg', j.ok, j.message);
        if (j.ok) { pending.brightness = ''; pending.rotation = ''; }
      } catch (e) { setMsg('displayMsg', false, 'Request failed'); }
    }

    async function savePower() {
      const body = new URLSearchParams();
      body.set('softwareEnabled', document.getElementById('softwareSwitch').checked ? '1' : '0');
      body.set('forcedOff', document.getElementById('forcedSwitch').checked ? '1' : '0');
      try {
        const r = await fetch('/api/power', { method: 'POST', body });
        const j = await r.json();
        setMsg('powerMsg', j.ok, j.message);
        if (j.ok) { pending.software = null; pending.force = null; }
      } catch (e) { setMsg('powerMsg', false, 'Request failed'); }
    }

    async function saveFan() {
      const rpm = document.getElementById('fanTarget').value;
      const body = new URLSearchParams();
      body.set('rpm', rpm);
      try {
        const r = await fetch('/api/fan', { method: 'POST', body });
        const j = await r.json();
        setMsg('fanMsg', j.ok, j.message);
        if (j.ok) { pending.fan = ''; }
      } catch (e) { setMsg('fanMsg', false, 'Request failed'); }
    }

    async function scanWifi() {
      setMsg('scanMsg', true, 'Scanning...');
      document.getElementById('scanList').innerHTML = '';
      try {
        const r = await fetch('/api/scan');
        const j = await r.json();
        if (j.running) { setTimeout(scanWifi, 700); return; }
        if (!j.ok) { setMsg('scanMsg', false, j.message || 'Scan failed'); return; }
        setMsg('scanMsg', true, 'Found ' + j.count + ' networks');
        const list = document.getElementById('scanList');
        (j.networks || []).forEach(n => {
          const btn = document.createElement('button');
          btn.type = 'button'; btn.className = 'net-item';
          btn.textContent = n.ssid + '  (RSSI ' + n.rssi + ' dBm' + (n.secure ? ', locked' : ', open') + ')';
          btn.onclick = () => {
            pending.staSsid = n.ssid;
            document.getElementById('staSsid').value = n.ssid;
            setMsg('scanMsg', true, 'Selected: ' + n.ssid);
            document.getElementById('staPass').focus();
          };
          list.appendChild(btn);
        });
      } catch (e) { setMsg('scanMsg', false, 'Scan failed'); }
    }

    // ── Simulated data for preview ──
    let simTime = 0;
    function simRefresh() {
      simTime++;

      // Input side: 220V AC, varying load
      const inV = 220 + (Math.random() - 0.5) * 3;
      const inA = 1.5 + Math.sin(simTime * 0.08) * 0.8 + (Math.random() - 0.5) * 0.3;
      const inW = inV * inA;

      // Output side: 12V DC, follows input load
      const outV = 12.05 + (Math.random() - 0.5) * 0.1;
      const outA = inW / outV * 0.9 + (Math.random() - 0.5) * 0.5;
      const outW = outV * outA;

      // C3 remote sensor: branch circuit
      const c3V = 5.02 + (Math.random() - 0.5) * 0.06;
      const c3A = 0.3 + Math.abs(Math.sin(simTime * 0.12)) * 1.5 + (Math.random() - 0.5) * 0.15;
      const c3W = c3V * c3A;

      // Push to rings
      ringPush(rings.inV, inV, MAX_SAMPLES); ringPush(rings.inA, inA, MAX_SAMPLES); ringPush(rings.inW, inW, MAX_SAMPLES);
      ringPush(rings.outV, outV, MAX_SAMPLES); ringPush(rings.outA, outA, MAX_SAMPLES); ringPush(rings.outW, outW, MAX_SAMPLES);
      ringPush(rings.c3V, c3V, MAX_SAMPLES); ringPush(rings.c3A, c3A, MAX_SAMPLES); ringPush(rings.c3W, c3W, MAX_SAMPLES);

      // Update text
      document.getElementById('vin').textContent = inV.toFixed(2);
      document.getElementById('iin').textContent = inA.toFixed(2);
      document.getElementById('pin').textContent = inW.toFixed(1);
      document.getElementById('vout').textContent = outV.toFixed(2);
      document.getElementById('iout').textContent = outA.toFixed(2);
      document.getElementById('pout').textContent = outW.toFixed(1);
      document.getElementById('c3Vin').textContent = c3V.toFixed(2);
      document.getElementById('c3Iin').textContent = c3A.toFixed(2);
      document.getElementById('c3Pin').textContent = c3W.toFixed(1);
      document.getElementById('c3Status').textContent = 'Online | Seq: ' + simTime;

      // Update gauges
      const temp1 = 42 + Math.sin(simTime * 0.03) * 6 + (Math.random() - 0.5) * 2;
      const temp2 = 38 + Math.sin(simTime * 0.05) * 4 + (Math.random() - 0.5) * 1.5;
      const fanRpm = 3000 + Math.abs(Math.sin(simTime * 0.07)) * 4000;
      const eff = (outW / inW * 100);
      document.getElementById('temp1').textContent = temp1.toFixed(1);
      document.getElementById('temp2').textContent = temp2.toFixed(1);
      document.getElementById('fan').textContent = Math.round(fanRpm);
      document.getElementById('effPct').textContent = eff.toFixed(1) + '%';
      setGauge('temp1Gauge', temp1, 120);
      setGauge('temp2Gauge', temp2, 120);
      setGauge('fanGauge', fanRpm, 17000);
      setGauge('effGauge', eff, 100);

      // Update device info with static data
      document.getElementById('setupSpn').textContent = 'SPN-12345';
      document.getElementById('setupMfg').textContent = '2024-06';
      document.getElementById('setupMfr').textContent = 'Delta Electronics';
      document.getElementById('setupName').textContent = 'DPS-750RB';
      document.getElementById('setupOkn').textContent = 'OK-001';
      document.getElementById('setupCt').textContent = '2406';
      document.getElementById('wifiInfo').textContent = 'AP: CSPS-Power (192.168.4.1) | STA: MyWiFi (192.168.1.100)';
      document.getElementById('outputStatus').innerHTML = '<span class="status-dot green"></span> ON';
      document.getElementById('powerInfo').textContent = 'Output: ON | Force: Enabled';

      // Draw sparklines
      drawSparkline('inputSpark', rings.inV, rings.inA, rings.inW);
      drawSparkline('outputSpark', rings.outV, rings.outA, rings.outW);
      drawSparkline('c3Spark', rings.c3V, rings.c3A, rings.c3W);
    }

    setInterval(refresh, 1000);
    refresh();
  </script>

</body>
</html>
)HTML";
}

String json_escape(const String &s)
{
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i)
  {
    const char c = s.charAt(i);
    if (c == '\\' || c == '"')
    {
      out += '\\';
      out += c;
    }
    else
    {
      out += c;
    }
  }
  return out;
}

void load_wifi_config()
{
  g_prefs.begin(kPrefsNs, true);
  g_ap_ssid = g_prefs.getString("ap_ssid", kDefaultApSsid);
  g_ap_pass = g_prefs.getString("ap_pass", kDefaultApPass);
  g_sta_ssid = g_prefs.getString("sta_ssid", "");
  g_sta_pass = g_prefs.getString("sta_pass", "");
  g_prefs.end();

  if (g_ap_ssid.isEmpty())
  {
    g_ap_ssid = kDefaultApSsid;
  }
  if (g_ap_pass.length() < 8)
  {
    g_ap_pass = kDefaultApPass;
  }
}

void save_wifi_config()
{
  g_prefs.begin(kPrefsNs, false);
  g_prefs.putString("ap_ssid", g_ap_ssid);
  g_prefs.putString("ap_pass", g_ap_pass);
  g_prefs.putString("sta_ssid", g_sta_ssid);
  g_prefs.putString("sta_pass", g_sta_pass);
  g_prefs.end();
}

void apply_wifi()
{
  Serial.println("WiFi: applying configuration");

  WiFi.persistent(false);
  Serial.println("WiFi: set mode NULL");
  WiFi.mode(WIFI_MODE_NULL);
  delay(10);
  WiFi.setAutoReconnect(true);
  delay(1);
  Serial.println("WiFi: stop old AP");
  WiFi.softAPdisconnect(true);
  delay(10);
  Serial.println("WiFi: disconnect STA");
  WiFi.disconnect(true, false);
  delay(10);

  Serial.println("WiFi: start AP mode");
  WiFi.mode(WIFI_AP);
  delay(10);
  const bool ap_ok = WiFi.softAP(g_ap_ssid.c_str(), g_ap_pass.c_str());
  Serial.printf("WiFi: softAP result=%s, IP=%s\n",
                ap_ok ? "OK" : "FAIL",
                WiFi.softAPIP().toString().c_str());
  delay(1);
  g_wifi_started_ms = millis();

  g_sta_connect_pending = false;
  if (!g_sta_ssid.isEmpty())
  {
    g_sta_connect_pending = true;
    g_sta_connect_requested_ms = millis();
    Serial.printf("WiFi: STA deferred for SSID=%s\n", g_sta_ssid.c_str());
  }
}

void start_sta_connection()
{
  Serial.println("WiFi: enabling STA");
  WiFi.mode(WIFI_AP_STA);
  delay(10);
  WiFi.begin(g_sta_ssid.c_str(), g_sta_pass.c_str());
  g_last_sta_retry_ms = millis();
}

void schedule_wifi_apply(uint32_t delay_ms)
{
  g_wifi_apply_pending = true;
  g_wifi_apply_requested_ms = millis();
  g_wifi_apply_delay_ms = delay_ms;
}

void handle_status()
{
  const String sta_ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  const uint8_t brightness = display_get_brightness();
  const uint8_t rotation = display_get_rotation();
  const uint32_t brightness_percent = (static_cast<uint32_t>(brightness) * 100U + 127U) / 255U;
  const bool software_enabled = power_ctrl_software_enabled();
  const bool force_enabled = power_ctrl_force_enabled();
  const bool output_enabled = power_ctrl_output_enabled();

  String payload;
  payload.reserve(1024);
  payload += "{";
  payload += "\"ap_ssid\":\"" + json_escape(g_ap_ssid) + "\",";
  payload += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
  payload += "\"sta_ssid\":\"" + json_escape(g_sta_ssid) + "\",";
  payload += "\"sta_ip\":\"" + json_escape(sta_ip) + "\",";
  payload += "\"software_enabled\":" + String(software_enabled ? "true" : "false") + ",";
  payload += "\"force_enabled\":" + String(force_enabled ? "true" : "false") + ",";
  payload += "\"forced_off\":" + String(force_enabled ? "false" : "true") + ",";
  payload += "\"output_enabled\":" + String(output_enabled ? "true" : "false") + ",";
  payload += "\"setup_spn\":\"" + json_escape(g_setup_info.spare_part_no) + "\",";
  payload += "\"setup_mfg\":\"" + json_escape(g_setup_info.manufacture_date) + "\",";
  payload += "\"setup_mfr\":\"" + json_escape(g_setup_info.manufacturer) + "\",";
  payload += "\"setup_name\":\"" + json_escape(g_setup_info.power_name) + "\",";
  payload += "\"setup_okn\":\"" + json_escape(g_setup_info.option_kit_no) + "\",";
  payload += "\"setup_ct\":\"" + json_escape(g_setup_info.ct_date_codes) + "\",";
  payload += "\"brightness_percent\":" + String(brightness_percent) + ",";
  payload += "\"rotation\":" + String(rotation) + ",";
  payload += "\"fan_speed_rpm\":" + String(g_latest_data.fan_speed_rpm, 1) + ",";
  payload += "\"temp1_c\":" + String(g_latest_data.temp1_c, 2) + ",";
  payload += "\"temp2_c\":" + String(g_latest_data.temp2_c, 2) + ",";
  payload += "\"power_in_w\":" + String(g_latest_data.power_in_w, 2) + ",";
  payload += "\"power_out_w\":" + String(g_latest_data.power_out_w, 2) + ",";
  payload += "\"current_out_a\":" + String(g_latest_data.current_out_a, 3) + ",";
  payload += "\"current_in_a\":" + String(g_latest_data.current_in_a, 3) + ",";
  payload += "\"voltage_out_v\":" + String(g_latest_data.voltage_out_v, 3) + ",";
  payload += "\"voltage_in_v\":" + String(g_latest_data.voltage_in_v, 3) + ",";
  payload += "\"efficiency_percent\":" + String(g_latest_data.efficiency_percent, 2) + ",";
  payload += "\"c3_voltage_v\":" + String(g_c3_data.valid ? g_c3_data.voltage_v : 0.0f, 2) + ",";
  payload += "\"c3_current_a\":" + String(g_c3_data.valid ? g_c3_data.current_a : 0.0f, 3) + ",";
  payload += "\"c3_power_w\":"   + String(g_c3_data.valid ? g_c3_data.power_w   : 0.0f, 2) + ",";
  payload += "\"c3_sequence\":"  + String(g_c3_data.sequence) + ",";
  payload += "\"c3_online\":"    + String(g_c3_data.valid ? "true" : "false") + ",";
  payload += "\"shutdown_status\":" + String(g_shutdown_status) + ",";
  payload += "\"shutdown_reason\":\"" + json_escape(g_shutdown_reason) + "\",";
  payload += "\"warning_status\":" + String(g_warning_status) + ",";
  payload += "\"warning_reason\":\"" + json_escape(g_warning_reason) + "\"";
  payload += "}";

  g_server.send(200, "application/json", payload);
}

void handle_display_post()
{
  if (!g_server.hasArg("brightness") || !g_server.hasArg("rotation"))
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"Missing display parameters\"}");
    return;
  }

  int brightness_percent = 0;
  int rotation = 0;
  if (!parse_uint_arg(g_server.arg("brightness"), brightness_percent) ||
      !parse_uint_arg(g_server.arg("rotation"), rotation))
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"Invalid display parameters\"}");
    return;
  }

  if (brightness_percent < 0 || brightness_percent > 100)
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"Brightness must be 0-100\"}");
    return;
  }

  if (rotation < 0 || rotation > 3)
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"Rotation must be 0-3\"}");
    return;
  }

  const uint8_t brightness = static_cast<uint8_t>((static_cast<uint32_t>(brightness_percent) * 255U) / 100U);
  display_set_brightness(brightness);
  display_set_rotation(static_cast<uint8_t>(rotation));

  g_server.send(200, "application/json", "{\"ok\":true,\"message\":\"Display settings applied.\"}");
}

void handle_power_post()
{
  if (!g_server.hasArg("softwareEnabled") || !g_server.hasArg("forcedOff"))
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"Missing power parameters\"}");
    return;
  }

  int software_enabled = 0;
  int forced_off = 0;
  if (!parse_uint_arg(g_server.arg("softwareEnabled"), software_enabled) ||
      !parse_uint_arg(g_server.arg("forcedOff"), forced_off) ||
      (software_enabled != 0 && software_enabled != 1) ||
      (forced_off != 0 && forced_off != 1))
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"Invalid power parameters\"}");
    return;
  }

  const bool requested_forced_off = (forced_off == 1);
  const bool requested_software_enabled = (software_enabled == 1);

  power_ctrl_set_force_enabled(!requested_forced_off);
  if (requested_forced_off)
  {
    power_ctrl_set_software_enabled(false);
    g_server.send(200, "application/json", "{\"ok\":true,\"message\":\"Forced shutdown enabled. Software power turned off.\"}");
    return;
  }

  power_ctrl_set_software_enabled(requested_software_enabled);
  g_server.send(200, "application/json", "{\"ok\":true,\"message\":\"Power control applied.\"}");
}

void handle_wifi_post()
{
  const String new_sta_ssid = g_server.arg("staSsid");
  const String new_sta_pass = g_server.arg("staPass");
  const String new_ap_ssid = g_server.arg("apSsid");
  const String new_ap_pass = g_server.arg("apPass");

  if (new_sta_ssid.length() > kMaxSsidLen || new_ap_ssid.length() > kMaxSsidLen)
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"SSID too long\"}");
    return;
  }

  if (new_sta_pass.length() > kMaxPassLen || new_ap_pass.length() > kMaxPassLen)
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"Password too long\"}");
    return;
  }

  if (!new_ap_ssid.isEmpty())
  {
    g_ap_ssid = new_ap_ssid;
  }

  if (!new_ap_pass.isEmpty())
  {
    if (new_ap_pass.length() < 8)
    {
      g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"AP password must be at least 8 chars\"}");
      return;
    }
    g_ap_pass = new_ap_pass;
  }

  g_sta_ssid = new_sta_ssid;
  g_sta_pass = new_sta_pass;

  save_wifi_config();
  apply_wifi();

  g_server.send(200, "application/json", "{\"ok\":true,\"message\":\"Saved. Wi-Fi settings applied.\"}");
}

void handle_fan_post()
{
  if (!g_server.hasArg("rpm"))
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"Missing rpm parameter\"}");
    return;
  }

  int rpm = 0;
  if (!parse_uint_arg(g_server.arg("rpm"), rpm))
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"Invalid rpm value\"}");
    return;
  }

  if (rpm < 0 || rpm > 65535)
  {
    g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"RPM out of range (0-65535)\"}");
    return;
  }

  const bool ok = pmbus_set_fan_rpm(static_cast<uint16_t>(rpm));
  if (ok)
  {
    g_server.send(200, "application/json", "{\"ok\":true,\"message\":\"Fan speed set to " + String(rpm) + " RPM\"}");
  }
  else
  {
    g_server.send(500, "application/json", "{\"ok\":false,\"message\":\"I2C write failed\"}");
  }
}

void handle_wifi_scan()
{
  const int scan_state = WiFi.scanComplete();
  if (scan_state == WIFI_SCAN_RUNNING)
  {
    g_server.send(200, "application/json", "{\"ok\":true,\"running\":true,\"message\":\"Scanning...\"}");
    return;
  }

  if (scan_state == WIFI_SCAN_FAILED)
  {
    const int start_result = WiFi.scanNetworks(true, true);
    if (start_result != WIFI_SCAN_RUNNING)
    {
      g_server.send(500, "application/json", "{\"ok\":false,\"message\":\"Failed to start scan\"}");
      return;
    }

    g_server.send(200, "application/json", "{\"ok\":true,\"running\":true,\"message\":\"Scanning...\"}");
    return;
  }

  const int n = scan_state;
  if (n < 0)
  {
    g_server.send(500, "application/json", "{\"ok\":false,\"message\":\"Scan failed\"}");
    return;
  }

  String payload;
  payload.reserve(1536);
  payload += "{\"ok\":true,\"count\":" + String(n) + ",\"networks\":[";

  bool first = true;
  int emitted = 0;
  for (int i = 0; i < n && emitted < kMaxScanResults; ++i)
  {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty())
    {
      continue;
    }

    if (!first)
    {
      payload += ",";
    }
    first = false;

    payload += "{\"ssid\":\"" + json_escape(ssid) + "\",";
    payload += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    payload += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
    ++emitted;
  }

  payload += "]}";
  g_server.send(200, "application/json", payload);
  WiFi.scanDelete();
}

void setup_routes()
{
  g_server.on("/", HTTP_GET, []() {
    g_server.send(200, "text/html", html_page());
  });

  g_server.on("/api/status", HTTP_GET, handle_status);
  g_server.on("/api/scan", HTTP_GET, handle_wifi_scan);
  g_server.on("/api/wifi", HTTP_POST, handle_wifi_post);
  g_server.on("/api/display", HTTP_POST, handle_display_post);
  g_server.on("/api/power", HTTP_POST, handle_power_post);
  g_server.on("/api/fan", HTTP_POST, handle_fan_post);

  g_server.onNotFound([]() {
    g_server.send(404, "text/plain", "Not found");
  });
}
} // namespace

void wifi_portal_init()
{
  load_wifi_config();
  setup_routes();
  schedule_wifi_apply(250U);
  Serial.println("WiFi portal init complete, startup deferred to task");
}

void wifi_portal_task()
{
  if (g_wifi_apply_pending)
  {
    const uint32_t now = millis();
    if (now - g_wifi_apply_requested_ms >= g_wifi_apply_delay_ms)
    {
      g_wifi_apply_pending = false;
      apply_wifi();

      if (!g_server_started)
      {
        g_server.begin();
        g_server_started = true;
      }

      Serial.printf("WiFi AP started: SSID=%s, IP=%s\n", g_ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());
      if (!g_sta_ssid.isEmpty())
      {
        Serial.printf("WiFi STA connecting: SSID=%s\n", g_sta_ssid.c_str());
      }
    }
  }

  if (!g_server_started)
  {
    return;
  }

  if (g_sta_connect_pending && !g_sta_ssid.isEmpty())
  {
    const uint32_t now = millis();
    if (now - g_sta_connect_requested_ms >= 500U)
    {
      g_sta_connect_pending = false;
      start_sta_connection();
    }
  }

  g_server.handleClient();

  // Keep trying STA connection in background when credentials are available.
  if (!g_sta_ssid.isEmpty() && WiFi.status() != WL_CONNECTED)
  {
    const uint32_t now = millis();
    if (now - g_last_sta_retry_ms >= 10000U)
    {
      Serial.println("WiFi: retry STA begin");
      WiFi.begin(g_sta_ssid.c_str(), g_sta_pass.c_str());
      g_last_sta_retry_ms = now;
    }
  }
}

void wifi_portal_set_latest_data(const pmbus_data_t *data)
{
  if (data == nullptr)
  {
    return;
  }
  g_latest_data = *data;
}

void wifi_portal_set_setup_info(const pmbus_setup_info_t *info)
{
  if (info == nullptr)
  {
    return;
  }

  g_setup_info = *info;
}

void wifi_portal_set_c3_data(const c3_sensor_data_t *data)
{
  if (data == nullptr)
  {
    g_c3_data.valid = false;
    return;
  }
  g_c3_data = *data;
}

void wifi_portal_set_power_status(uint16_t shutdown_status, uint16_t warning_status, const char *shutdown_reason, const char *warning_reason)
{
  g_shutdown_status = shutdown_status;
  g_warning_status = warning_status;
  g_shutdown_reason = shutdown_reason ? shutdown_reason : "";
  g_warning_reason = warning_reason ? warning_reason : "";
}

void wifi_portal_start()
{
  schedule_wifi_apply(0U);
}

void wifi_portal_clear_config()
{
  g_sta_ssid = "";
  g_sta_pass = "";
  g_ap_ssid = kDefaultApSsid;
  g_ap_pass = kDefaultApPass;
  save_wifi_config();
}

String wifi_portal_get_ap_ssid()
{
  return g_ap_ssid;
}

String wifi_portal_get_ap_ip()
{
  return WiFi.softAPIP().toString();
}

bool wifi_portal_ap_active()
{
  return (WiFi.getMode() & WIFI_AP) != 0;
}

bool wifi_portal_has_sta_config()
{
  return !g_sta_ssid.isEmpty();
}

String wifi_portal_get_sta_ssid()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return WiFi.SSID();
  }
  return g_sta_ssid;
}

bool wifi_portal_sta_connected()
{
  return WiFi.status() == WL_CONNECTED;
}

String wifi_portal_get_sta_ip()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return "";
  }
  return WiFi.localIP().toString();
}

uint32_t wifi_portal_get_uptime_seconds()
{
  return (millis() - g_wifi_started_ms) / 1000U;
}
