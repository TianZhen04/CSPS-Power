#ifndef ESPNOW_BRIDGE_H
#define ESPNOW_BRIDGE_H

#include <Arduino.h>
#include <stdint.h>

//C3 数据格式
//[0xCA 0xFE] [sequence:2B] [voltage:float32] [current:float32] [checksum:XOR]
//总长 13 字节，广播到 FF:FF:FF:FF:FF:FF

struct c3_sensor_data_t
{
  float    voltage_v;
  float    current_a;
  float    power_w;
  uint16_t sequence;
  uint32_t last_seen_ms;
  bool     valid;
};

void espnow_bridge_init();
void espnow_bridge_task();
const c3_sensor_data_t *espnow_bridge_get_latest_data();
bool espnow_bridge_has_data();

#endif
