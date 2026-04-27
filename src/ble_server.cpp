#include <ble_server.h>

#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <Preferences.h>

namespace
{
static constexpr const char *kDeviceName = "CSPS-Power-Host";
static constexpr const char *kDefaultTargetName = "CSPS-Power-BLE";
static constexpr const char *kDefaultServiceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char *kDefaultDataCharUuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr uint32_t kScanIntervalMs = 10000U;
static constexpr uint32_t kScanDurationSeconds = 1U;
static constexpr uint32_t kReadIntervalMs = 1000U;
static constexpr const char *kPrefsNs = "ble_cfg";
static constexpr const char *kPrefsEnabledKey = "enabled";

BLEScan *g_scan = nullptr;
BLEClient *g_client = nullptr;
BLEAdvertisedDevice *g_target_device = nullptr;
BLERemoteCharacteristic *g_remote_data_char = nullptr;
String g_target_name = kDefaultTargetName;
uint32_t g_last_scan_ms = 0;
uint32_t g_last_read_ms = 0;
bool g_connected = false;
bool g_connect_pending = false;
bool g_enabled = true;
bool g_ble_initialized = false;
bool g_connected_session_ready = false;

void on_remote_data_notify(
    BLERemoteCharacteristic *characteristic,
    uint8_t *data,
    size_t length,
    bool is_notify)
{
  (void)characteristic;
  (void)is_notify;

  String payload;
  payload.reserve(length + 1U);
  for (size_t i = 0; i < length; ++i)
  {
    payload += static_cast<char>(data[i]);
  }

  Serial.printf("BLE rx notify: %s\n", payload.c_str());
}

class ClientCallbacks : public BLEClientCallbacks
{
public:
  void onConnect(BLEClient *client) override
  {
    (void)client;
    g_connected = true;
    g_connected_session_ready = false;
    g_remote_data_char = nullptr;
    g_last_read_ms = 0;
    Serial.println("BLE host connected to target");
  }

  void onDisconnect(BLEClient *client) override
  {
    (void)client;
    g_connected = false;
    g_connected_session_ready = false;
    g_remote_data_char = nullptr;
    g_last_read_ms = 0;
    Serial.println("BLE host disconnected from target");
  }
};

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks
{
public:
  void onResult(BLEAdvertisedDevice advertised_device) override
  {
    if (!advertised_device.haveName())
    {
      return;
    }

    const String found_name = String(advertised_device.getName().c_str());
    if (found_name != g_target_name)
    {
      return;
    }

    Serial.printf(
        "BLE host matched target '%s', RSSI=%d\n",
        found_name.c_str(),
        advertised_device.getRSSI());

    if (g_target_device != nullptr)
    {
      delete g_target_device;
      g_target_device = nullptr;
    }
    g_target_device = new BLEAdvertisedDevice(advertised_device);
    g_connect_pending = true;

    if (g_scan != nullptr)
    {
      g_scan->stop();
    }
  }
};

void connect_target_if_needed()
{
  if (!g_connect_pending || g_target_device == nullptr)
  {
    return;
  }

  g_connect_pending = false;

  if (g_client == nullptr)
  {
    g_client = BLEDevice::createClient();
    g_client->setClientCallbacks(new ClientCallbacks());
  }

  if (g_client->isConnected())
  {
    g_connected = true;
    delete g_target_device;
    g_target_device = nullptr;
    return;
  }

  Serial.printf("BLE host connecting to '%s'...\n", g_target_name.c_str());
  const bool ok = g_client->connect(g_target_device);
  if (ok)
  {
    g_connected = true;
    Serial.println("BLE host connect success");
  }
  else
  {
    g_connected = false;
    Serial.println("BLE host connect failed");
  }

  delete g_target_device;
  g_target_device = nullptr;
}

void process_connected_slave_info()
{
  if (!g_connected || g_client == nullptr || !g_client->isConnected())
  {
    g_connected_session_ready = false;
    g_remote_data_char = nullptr;
    return;
  }

  if (!g_connected_session_ready)
  {
    BLERemoteService *service = g_client->getService(BLEUUID(kDefaultServiceUuid));
    if (service == nullptr)
    {
      Serial.println("BLE host: target service not found");
      return;
    }

    g_remote_data_char = service->getCharacteristic(BLEUUID(kDefaultDataCharUuid));
    if (g_remote_data_char == nullptr)
    {
      Serial.println("BLE host: target characteristic not found");
      return;
    }

    if (g_remote_data_char->canNotify())
    {
      g_remote_data_char->registerForNotify(on_remote_data_notify);
      Serial.println("BLE host: notify subscribed");
    }

    g_last_read_ms = 0;
    g_connected_session_ready = true;
    Serial.println("BLE host: connected session ready");
  }

  if (g_remote_data_char == nullptr)
  {
    return;
  }

  const uint32_t now_ms = millis();
  if ((now_ms - g_last_read_ms) < kReadIntervalMs)
  {
    return;
  }

  g_last_read_ms = now_ms;
  if (!g_remote_data_char->canRead())
  {
    return;
  }

  const std::string value = g_remote_data_char->readValue();
  if (!value.empty())
  {
    Serial.printf("BLE rx read: %s\n", value.c_str());
  }
}

void stop_ble_host()
{
  g_connect_pending = false;
  g_connected = false;
  g_connected_session_ready = false;
  g_remote_data_char = nullptr;
  g_last_read_ms = 0;

  if (g_scan != nullptr)
  {
    g_scan->stop();
  }

  if (g_client != nullptr && g_client->isConnected())
  {
    g_client->disconnect();
  }

  if (g_target_device != nullptr)
  {
    delete g_target_device;
    g_target_device = nullptr;
  }

  if (g_ble_initialized)
  {
    BLEDevice::deinit(true);
    g_ble_initialized = false;
  }

  g_scan = nullptr;
  g_client = nullptr;
}

void start_ble_host_if_needed()
{
  if (!g_enabled || g_ble_initialized)
  {
    return;
  }

  BLEDevice::init(kDeviceName);

  g_scan = BLEDevice::getScan();
  g_scan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
  g_scan->setActiveScan(true);
  g_scan->setInterval(100);
  g_scan->setWindow(99);

  g_ble_initialized = true;
  g_last_scan_ms = 0;
  Serial.printf("BLE host started, device name: %s\n", kDeviceName);
  Serial.printf("BLE host target name: %s\n", g_target_name.c_str());
}

void ble_store_enabled()
{
  Preferences prefs;
  if (prefs.begin(kPrefsNs, false))
  {
    prefs.putBool(kPrefsEnabledKey, g_enabled);
    prefs.end();
  }
}

void ble_load_enabled()
{
  Preferences prefs;
  if (prefs.begin(kPrefsNs, true))
  {
    g_enabled = prefs.getBool(kPrefsEnabledKey, true);
    prefs.end();
  }
}
}

void ble_server_init()
{
  ble_load_enabled();
  start_ble_host_if_needed();
}

void ble_server_set_target_name(const char *name)
{
  if (name == nullptr || name[0] == '\0')
  {
    return;
  }

  g_target_name = name;
  Serial.printf("BLE host target set to: %s\n", g_target_name.c_str());
}

void ble_server_set_enabled(bool enabled)
{
  if (g_enabled == enabled)
  {
    return;
  }

  g_enabled = enabled;
  if (g_enabled)
  {
    Serial.println("BLE host enabled");
    start_ble_host_if_needed();
  }
  else
  {
    Serial.println("BLE host disabled");
    stop_ble_host();
  }

  ble_store_enabled();
}

bool ble_server_enabled()
{
  return g_enabled;
}

void ble_server_task()
{
  // 用户在 UI/配置中关闭了蓝牙，任务直接空转返回。
  if (!g_enabled)
  {
    return;
  }

  // 延迟初始化：如果之前反初始化过，这里重新拉起 BLE 主机栈。
  if (!g_ble_initialized)
  {
    start_ble_host_if_needed();
  }

  // 主机流程依赖扫描对象。
  if (g_scan == nullptr)
  {
    return;
  }

  // 与底层客户端连接状态保持一致。
  if (g_connected && g_client != nullptr && !g_client->isConnected())
  {
    g_connected = false;
    g_connected_session_ready = false;
    g_remote_data_char = nullptr;
  }

  // 如果前面扫描已经命中目标，优先尝试连接。
  connect_target_if_needed();
  if (g_connected)
  {
    process_connected_slave_info();
    // 已连接时跳过扫描，减少功耗和延迟。
    return;
  }

  // 扫描节流：避免每个 loop 周期都发起扫描。
  const uint32_t now_ms = millis();
  if ((now_ms - g_last_scan_ms) < kScanIntervalMs)
  {
    return;
  }

  g_last_scan_ms = now_ms;
  // 开始新一轮扫描。
  g_connect_pending = false;
  BLEScanResults found = g_scan->start(kScanDurationSeconds, false);
  Serial.printf("BLE host scan done, found %d devices\n", found.getCount());

  // 扫描回调里可能已经命中目标，这里立即尝试连接。
  connect_target_if_needed();

  // 释放扫描结果缓存，保持内存占用稳定。
  g_scan->clearResults();
}

bool ble_server_connected()
{
  return g_connected;
}
