#include <espnow_bridge.h>

#include <esp_now.h>
#include <WiFi.h>
#include <cstring>

namespace
{

struct __attribute__((packed)) csps_sensor_packet_t
{
  uint8_t  magic[2];
  uint16_t sequence;
  float    voltage_v;
  float    current_a;
  uint8_t  checksum;
};

static constexpr uint8_t  kMagic0        = 0xCA;
static constexpr uint8_t  kMagic1        = 0xFE;
static constexpr size_t   kPacketSize    = sizeof(csps_sensor_packet_t);
static constexpr uint32_t kDataTimeoutMs = 5000U;

static c3_sensor_data_t g_latest_c3_data = {};
static csps_sensor_packet_t g_rx_buffer = {};
static volatile bool g_packet_pending = false;
static bool g_espnow_initialized = false;

static uint8_t compute_checksum(const csps_sensor_packet_t &pkt)
{
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&pkt);
  uint8_t csum = 0;
  for (size_t i = 0; i < kPacketSize - 1U; ++i)
  {
    csum ^= bytes[i];
  }
  return csum;
}

static void on_espnow_recv(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
  (void)mac_addr;

  if (data_len != static_cast<int>(kPacketSize))
  {
    return;
  }

  std::memcpy(&g_rx_buffer, data, kPacketSize);
  g_packet_pending = true;
}

static void on_espnow_send(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  (void)mac_addr;
  (void)status;
}

static void try_init_espnow()
{
  if (g_espnow_initialized)
  {
    return;
  }

  if ((WiFi.getMode() & WIFI_AP) == 0)
  {
    return;
  }

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW: init failed, will retry");
    return;
  }

  esp_now_register_recv_cb(on_espnow_recv);
  esp_now_register_send_cb(on_espnow_send);

  esp_now_peer_info_t peer_info = {};
  std::memset(peer_info.peer_addr, 0xFF, 6);
  peer_info.channel = 0;
  peer_info.encrypt = false;
  peer_info.ifidx   = WIFI_IF_AP;

  if (esp_now_add_peer(&peer_info) != ESP_OK)
  {
    Serial.println("ESP-NOW: add broadcast peer failed");
  }

  g_espnow_initialized = true;
  Serial.println("ESP-NOW: initialized, listening for C3 broadcasts");
}

static void process_pending_packet()
{
  if (!g_packet_pending)
  {
    return;
  }
  g_packet_pending = false;

  const csps_sensor_packet_t &pkt = g_rx_buffer;

  if (pkt.magic[0] != kMagic0 || pkt.magic[1] != kMagic1)
  {
    return;
  }

  if (pkt.checksum != compute_checksum(pkt))
  {
    return;
  }

  if (pkt.voltage_v < 0.0f || pkt.voltage_v > 500.0f ||
      pkt.current_a < 0.0f || pkt.current_a > 100.0f)
  {
    return;
  }

  g_latest_c3_data.voltage_v    = pkt.voltage_v;
  g_latest_c3_data.current_a    = pkt.current_a;
  g_latest_c3_data.power_w      = pkt.voltage_v * pkt.current_a;
  g_latest_c3_data.sequence     = pkt.sequence;
  g_latest_c3_data.last_seen_ms = millis();
  g_latest_c3_data.valid        = true;
}

static void check_stale_data()
{
  if (!g_latest_c3_data.valid)
  {
    return;
  }

  if (millis() - g_latest_c3_data.last_seen_ms >= kDataTimeoutMs)
  {
    g_latest_c3_data.valid = false;
  }
}

} // namespace

void espnow_bridge_init()
{
  Serial.println("ESP-NOW bridge init (lazy)");
}

void espnow_bridge_task()
{
  try_init_espnow();

  if (!g_espnow_initialized)
  {
    return;
  }

  process_pending_packet();
  check_stale_data();
}

const c3_sensor_data_t *espnow_bridge_get_latest_data()
{
  if (!g_latest_c3_data.valid)
  {
    return nullptr;
  }
  return &g_latest_c3_data;
}

bool espnow_bridge_has_data()
{
  return g_latest_c3_data.valid;
}
