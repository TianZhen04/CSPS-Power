#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <Arduino.h>
#include <pmbus.h>
#include <espnow_bridge.h>

void wifi_portal_init();
void wifi_portal_task();
void wifi_portal_set_latest_data(const pmbus_data_t *data);
void wifi_portal_set_setup_info(const pmbus_setup_info_t *info);
void wifi_portal_set_c3_data(const c3_sensor_data_t *data);

void wifi_portal_start();
void wifi_portal_clear_config();

String wifi_portal_get_ap_ssid();
String wifi_portal_get_ap_ip();
bool wifi_portal_ap_active();
bool wifi_portal_has_sta_config();
String wifi_portal_get_sta_ssid();
bool wifi_portal_sta_connected();
String wifi_portal_get_sta_ip();
uint32_t wifi_portal_get_uptime_seconds();

#endif
