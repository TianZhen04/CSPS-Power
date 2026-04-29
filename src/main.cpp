#include <Arduino.h>
#include <Esp.h>
#include <beep.h>
#include <ble_server.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <display.h>
#include <led.h>
#include <ntc_temp.h>
#include <power_ctrl.h>
#include <pmbus.h>
#include <ui.h>
#include <wifi_portal.h>
#include <espnow_bridge.h>

static pmbus_data_t g_pmbus_data = {
  0.0f,  // fan_speed_rpm
  0.0f,  // temp1_c
  0.0f,  // temp2_c
  0.0f,  // power_in_w
  0.0f,  // power_out_w
  0.0f,  // current_out_a
  0.0f,  // current_in_a
  0.0f,  // voltage_out_v
  0.0f,  // voltage_in_v
  0.0f   // efficiency_percent
};
static pmbus_setup_info_t g_pmbus_setup_info = {};

static const char *reset_reason_to_string(esp_reset_reason_t reason)
{
  switch (reason)
  {
  case ESP_RST_POWERON:
    return "POWERON";
  case ESP_RST_EXT:
    return "EXTERNAL";
  case ESP_RST_SW:
    return "SOFTWARE";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INT_WDT";
  case ESP_RST_TASK_WDT:
    return "TASK_WDT";
  case ESP_RST_WDT:
    return "WDT";
  case ESP_RST_DEEPSLEEP:
    return "DEEPSLEEP";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_SDIO:
    return "SDIO";
  default:
    return "UNKNOWN";
  }
}

static void pmbus_setup_print_info()
{
  Serial.printf("Spare Part No: %s\n", g_pmbus_setup_info.spare_part_no.c_str());
  Serial.printf("Manufacture Date: %s\n", g_pmbus_setup_info.manufacture_date.c_str());
  Serial.printf("Manufacturer: %s\n", g_pmbus_setup_info.manufacturer.c_str());
  Serial.printf("Power Name: %s\n", g_pmbus_setup_info.power_name.c_str());
  Serial.printf("Option Kit No: %s\n", g_pmbus_setup_info.option_kit_no.c_str());
  Serial.printf("CT Date Codes: %s\n", g_pmbus_setup_info.ct_date_codes.c_str());
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  const esp_reset_reason_t reset_reason = esp_reset_reason();
  Serial.printf("Reset reason: %s (%d)\n", reset_reason_to_string(reset_reason), static_cast<int>(reset_reason));

  beep_init();
  Serial.printf("beep init success\n");
  led_init();
  Serial.printf("led init success\n");
  power_ctrl_init();
  Serial.printf("power control init success\n");
  ntc_temp_init();
  Serial.printf("ntc temp init success (ADC IO%d)\n", static_cast<int>(kNtcAdcPin));
  display_init();
  Serial.printf("display init success\n");
  pmbus_init();
  pmbus_get_setup_info(&g_pmbus_setup_info);
  pmbus_setup_print_info();
  wifi_portal_init();
  Serial.printf("wifi init success\n");
  wifi_portal_set_setup_info(&g_pmbus_setup_info);

  espnow_bridge_init();
  Serial.printf("espnow bridge init success\n");

  ble_server_set_target_name("CSPS-Power-BLE");
  ble_server_init();
  Serial.printf("ble init success\n");
}

void loop()
{
  static uint32_t last_pmbus_update_ms = 0;
  const uint32_t now_ms = millis();
  if (now_ms - last_pmbus_update_ms >= 1000U)
  {
    last_pmbus_update_ms = now_ms;
    if (!pmbus_update_data(&g_pmbus_data))
    {
      g_pmbus_data = pmbus_data_t{};
    }

    float ntc_temp_c = 0.0f;
    if (ntc_temp_read_c(&ntc_temp_c))
    {
      ui_set_board_temp(ntc_temp_c);
    }

    ui_update_power_data(&g_pmbus_data);
    wifi_portal_set_latest_data(&g_pmbus_data);
    wifi_portal_set_c3_data(espnow_bridge_get_latest_data());
  }

  static uint32_t start_ms = millis();
  const uint32_t elapsed_seconds = (millis() - start_ms) / 1000U;
  ui_update_work_time(elapsed_seconds);

  wifi_portal_task();
  espnow_bridge_task();
  ble_server_task();
  display_task_handler();
}
