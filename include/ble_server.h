#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <Arduino.h>
#include <pmbus.h>

void ble_server_init();
void ble_server_task();
void ble_server_set_target_name(const char *name);
void ble_server_set_enabled(bool enabled);
bool ble_server_enabled();
bool ble_server_connected();

#endif
