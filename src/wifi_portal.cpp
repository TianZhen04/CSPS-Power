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
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>CSPS Power Monitor</title>
  <style>
    body { font-family: Segoe UI, Arial, sans-serif; margin: 20px; background: #111; color: #eee; }
    h1 { margin: 0 0 12px; }
    .card { background: #1d1d1d; border: 1px solid #333; border-radius: 10px; padding: 14px; margin-bottom: 14px; }
    .grid { display: grid; grid-template-columns: repeat(2,minmax(120px,1fr)); gap: 8px; }
    label { display: block; margin: 8px 0 4px; }
    input { width: 100%; padding: 8px; border-radius: 6px; border: 1px solid #555; background: #0f0f0f; color: #eee; }
    input[type="range"] { padding: 0; }
    select { width: 100%; padding: 8px; border-radius: 6px; border: 1px solid #555; background: #0f0f0f; color: #eee; }
    button { margin-top: 10px; padding: 8px 12px; border: 0; border-radius: 6px; background: #179bff; color: white; cursor: pointer; }
    .small { opacity: .8; font-size: 13px; }
    .net-item { margin-top: 6px; padding: 8px 10px; width: 100%; text-align: left; background: #2a2a2a; border: 1px solid #4a4a4a; border-radius: 6px; color: #eee; cursor: pointer; }
    .net-item:hover { background: #333; }
    .row { display: flex; align-items: center; gap: 10px; }
    .row span { min-width: 48px; text-align: right; }
    .toggle-row { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-top: 10px; }
    .toggle-row label { margin: 0; }
    .toggle-row input[type="checkbox"] { width: auto; transform: scale(1.1); }
  </style>
</head>
<body>
  <h1>CSPS Power Monitor</h1>

  <div class="card">
    <div class="small">Open this page via ESP32 AP IP (default 192.168.4.1)</div>
    <div id="wifiInfo" class="small"></div>
  </div>

  <div class="card">
    <h3>Live Data</h3>
    <div class="grid">
      <div>Vin: <span id="vin">0</span> V</div>
      <div>Iin: <span id="iin">0</span> A</div>
      <div>Pin: <span id="pin">0</span> W</div>
      <div>Vout: <span id="vout">0</span> V</div>
      <div>Iout: <span id="iout">0</span> A</div>
      <div>Pout: <span id="pout">0</span> W</div>
      <div>Temp: <span id="temp">0</span> C</div>
      <div>Fan: <span id="fan">0</span> RPM</div>
    </div>
  </div>

  <div class="card">
    <h3>PMBus Setup Info</h3>
    <div class="grid">
      <div>Spare Part No: <span id="setupSpn">-</span></div>
      <div>Manufacture Date: <span id="setupMfg">-</span></div>
      <div>Manufacturer: <span id="setupMfr">-</span></div>
      <div>Power Name: <span id="setupName">-</span></div>
      <div>Option Kit No: <span id="setupOkn">-</span></div>
      <div>CT Date Codes: <span id="setupCt">-</span></div>
    </div>
  </div>

  <div class="card">
    <h3>Wi-Fi Settings</h3>
    <label>STA SSID</label><input id="staSsid" type="text">
    <button onclick="scanWifi()">Scan Wi-Fi</button>
    <div id="scanInfo" class="small"></div>
    <div id="scanList" class="small"></div>
    <label>STA Password</label><input id="staPass" type="password">
    <label>AP SSID</label><input id="apSsid" type="text">
    <label>AP Password (>=8 chars)</label><input id="apPass" type="password">
    <button onclick="saveWifi()">Save and Apply</button>
    <div id="msg" class="small"></div>
  </div>

  <div class="card">
    <h3>Display Settings</h3>
    <label for="brightness">Brightness</label>
    <div class="row">
      <input id="brightness" type="range" min="0" max="100" step="1">
      <span id="brightnessValue">0%</span>
    </div>
    <label for="rotation">Screen Rotation</label>
    <select id="rotation">
      <option value="0">0 deg</option>
      <option value="1">90 deg</option>
      <option value="2">180 deg</option>
      <option value="3">270 deg</option>
    </select>
    <button onclick="saveDisplay()">Apply Display Settings</button>
    <div id="displayMsg" class="small"></div>
  </div>

  <div class="card">
    <h3>Power Control</h3>
    <div class="toggle-row">
      <label for="softwareSwitch">Software Power</label>
      <input id="softwareSwitch" type="checkbox">
    </div>
    <div class="toggle-row">
      <label for="forcedSwitch">Forced Shutdown</label>
      <input id="forcedSwitch" type="checkbox">
    </div>
    <button onclick="savePower()">Apply Power Control</button>
    <div id="powerInfo" class="small"></div>
    <div id="powerMsg" class="small"></div>
  </div>

  <script>
    let pendingStaSsid = '';
    let pendingApSsid = '';
    let pendingBrightness = '';
    let pendingRotation = '';
    let pendingSoftwareSwitch = null;
    let pendingForcedSwitch = null;

    function syncInputValue(id, serverValue, pendingValue) {
      const input = document.getElementById(id);
      if (!input) {
        return;
      }

      if (document.activeElement === input) {
        return;
      }

      input.value = pendingValue !== '' ? pendingValue : (serverValue || '');
    }

    function syncRangeValue(id, serverValue, pendingValue) {
      const input = document.getElementById(id);
      if (!input) {
        return;
      }

      if (document.activeElement === input) {
        return;
      }

      const nextValue = pendingValue !== '' ? pendingValue : String(serverValue ?? 0);
      input.value = nextValue;
      if (id === 'brightness') {
        document.getElementById('brightnessValue').textContent = `${nextValue}%`;
      }
    }

    function syncSelectValue(id, serverValue, pendingValue) {
      const input = document.getElementById(id);
      if (!input) {
        return;
      }

      if (document.activeElement === input) {
        return;
      }

      input.value = pendingValue !== '' ? pendingValue : String(serverValue ?? 0);
    }

    function syncCheckboxValue(id, serverValue, pendingValue) {
      const input = document.getElementById(id);
      if (!input) {
        return;
      }

      if (document.activeElement === input) {
        return;
      }

      input.checked = pendingValue !== null ? pendingValue : !!serverValue;
    }

    async function refresh() {
      const r = await fetch('/api/status');
      const j = await r.json();
      document.getElementById('vin').textContent = j.voltage_in_v.toFixed(2);
      document.getElementById('iin').textContent = j.current_in_a.toFixed(2);
      document.getElementById('pin').textContent = j.power_in_w.toFixed(1);
      document.getElementById('vout').textContent = j.voltage_out_v.toFixed(2);
      document.getElementById('iout').textContent = j.current_out_a.toFixed(2);
      document.getElementById('pout').textContent = j.power_out_w.toFixed(1);
      document.getElementById('temp').textContent = j.temp1_c.toFixed(1);
      document.getElementById('fan').textContent = j.fan_speed_rpm.toFixed(0);
      document.getElementById('setupSpn').textContent = j.setup_spn || '-';
      document.getElementById('setupMfg').textContent = j.setup_mfg || '-';
      document.getElementById('setupMfr').textContent = j.setup_mfr || '-';
      document.getElementById('setupName').textContent = j.setup_name || '-';
      document.getElementById('setupOkn').textContent = j.setup_okn || '-';
      document.getElementById('setupCt').textContent = j.setup_ct || '-';
      document.getElementById('wifiInfo').textContent =
        `AP: ${j.ap_ssid} (${j.ap_ip}) | STA: ${j.sta_ssid || '-'} (${j.sta_ip || '-'})`;
      document.getElementById('powerInfo').textContent =
        `Output: ${j.output_enabled ? 'ON' : 'OFF'} | Force: ${j.force_enabled ? 'Enabled' : 'Forced Off'}`;

      syncInputValue('staSsid', j.sta_ssid, pendingStaSsid);
      syncInputValue('apSsid', j.ap_ssid, pendingApSsid);
      syncRangeValue('brightness', j.brightness_percent, pendingBrightness);
      syncSelectValue('rotation', j.rotation, pendingRotation);
      syncCheckboxValue('softwareSwitch', j.software_enabled, pendingSoftwareSwitch);
      syncCheckboxValue('forcedSwitch', j.forced_off, pendingForcedSwitch);
    }

    async function saveWifi() {
      const body = new URLSearchParams();
      body.set('staSsid', document.getElementById('staSsid').value);
      body.set('staPass', document.getElementById('staPass').value);
      body.set('apSsid', document.getElementById('apSsid').value);
      body.set('apPass', document.getElementById('apPass').value);
      const r = await fetch('/api/wifi', { method: 'POST', body });
      const j = await r.json();
      document.getElementById('msg').textContent = j.message;
      if (j.ok) {
        pendingStaSsid = '';
        pendingApSsid = '';
      }
      refresh();
    }

    async function saveDisplay() {
      const body = new URLSearchParams();
      body.set('brightness', document.getElementById('brightness').value);
      body.set('rotation', document.getElementById('rotation').value);
      const r = await fetch('/api/display', { method: 'POST', body });
      const j = await r.json();
      document.getElementById('displayMsg').textContent = j.message;
      if (j.ok) {
        pendingBrightness = '';
        pendingRotation = '';
      }
      refresh();
    }

    async function savePower() {
      const body = new URLSearchParams();
      body.set('softwareEnabled', document.getElementById('softwareSwitch').checked ? '1' : '0');
      body.set('forcedOff', document.getElementById('forcedSwitch').checked ? '1' : '0');
      const r = await fetch('/api/power', { method: 'POST', body });
      const j = await r.json();
      document.getElementById('powerMsg').textContent = j.message;
      if (j.ok) {
        pendingSoftwareSwitch = null;
        pendingForcedSwitch = null;
      }
      refresh();
    }

    async function scanWifi() {
      document.getElementById('scanInfo').textContent = 'Scanning...';
      document.getElementById('scanList').innerHTML = '';
      try {
        const r = await fetch('/api/scan');
        const j = await r.json();
        if (j.running) {
          document.getElementById('scanInfo').textContent = j.message || 'Scanning...';
          setTimeout(scanWifi, 700);
          return;
        }

        if (!j.ok) {
          document.getElementById('scanInfo').textContent = j.message || 'Scan failed';
          return;
        }

        document.getElementById('scanInfo').textContent = `Found ${j.count} networks`;
        const list = document.getElementById('scanList');
        (j.networks || []).forEach((n) => {
          const btn = document.createElement('button');
          btn.type = 'button';
          btn.className = 'net-item';
          btn.textContent = `${n.ssid}  (RSSI ${n.rssi} dBm${n.secure ? ', secure' : ', open'})`;
          btn.onclick = () => {
            pendingStaSsid = n.ssid;
            document.getElementById('staSsid').value = n.ssid;
            document.getElementById('scanInfo').textContent = `Selected: ${n.ssid}`;
            document.getElementById('staPass').focus();
          };
          list.appendChild(btn);
        });
      } catch (err) {
        document.getElementById('scanInfo').textContent = 'Scan failed';
      }
    }

    document.getElementById('staSsid').addEventListener('input', (e) => {
      pendingStaSsid = e.target.value;
    });

    document.getElementById('apSsid').addEventListener('input', (e) => {
      pendingApSsid = e.target.value;
    });

    document.getElementById('brightness').addEventListener('input', (e) => {
      pendingBrightness = e.target.value;
      document.getElementById('brightnessValue').textContent = `${e.target.value}%`;
    });

    document.getElementById('rotation').addEventListener('change', (e) => {
      pendingRotation = e.target.value;
    });

    document.getElementById('softwareSwitch').addEventListener('change', (e) => {
      pendingSoftwareSwitch = e.target.checked;
    });

    document.getElementById('forcedSwitch').addEventListener('change', (e) => {
      pendingForcedSwitch = e.target.checked;
    });

    refresh();
    setInterval(refresh, 1000);
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
  payload.reserve(896);
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
  payload += "\"efficiency_percent\":" + String(g_latest_data.efficiency_percent, 2);
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
