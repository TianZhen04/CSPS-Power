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
  0.0f,  // efficiency_percent
  0U     // psu_runtime_hours
};
static pmbus_setup_info_t g_pmbus_setup_info = {};

static bool g_shutdown_popup_shown = false;
static uint16_t g_warning_status = 0;

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
      wifi_portal_set_board_temp(ntc_temp_c);
    }

    ui_update_power_data(&g_pmbus_data);
    wifi_portal_set_latest_data(&g_pmbus_data);
    wifi_portal_set_c3_data(espnow_bridge_get_latest_data());

    // ── Shutdown status (0x04) ──
    const uint16_t shutdown_raw = pmbus_read_shutdown_status();
    if (shutdown_raw != 0 && !g_shutdown_popup_shown)
    {
      pmbus_shutdown_reason_t reason;
      pmbus_decode_shutdown_status(shutdown_raw, &reason);
      const char *reason_str = pmbus_get_shutdown_reason_string(&reason);
      ui_show_shutdown_popup(reason_str);
      g_shutdown_popup_shown = true;
      beep_play_shutdown_alert();
      Serial.printf("Power shutdown detected: 0x%04X (%s)\n", shutdown_raw, reason_str);
    }

    // ── Warning status (0x06) ──
    const uint16_t warning_raw = pmbus_read_warning_status();
    const char *warn_reason_str = "";
    if (warning_raw != g_warning_status)
    {
      g_warning_status = warning_raw;

      if (warning_raw != 0)
      {
        pmbus_warning_reason_t warn_reason;
        pmbus_decode_warning_status(warning_raw, &warn_reason);
        warn_reason_str = pmbus_get_warning_reason_string(&warn_reason);
        ui_show_warning_popup(warn_reason_str);
        ui_set_warning_icon_visible(true);
        beep_set_warning_active(true);
        Serial.printf("Power warning detected: 0x%04X (%s)\n", warning_raw, warn_reason_str);
      }
      else
      {
        ui_set_warning_icon_visible(false);
        beep_set_warning_active(false);
        Serial.println("Power warning cleared");
      }
    }
    else if (warning_raw != 0)
    {
      // Persist the reason string for web even when popup already shown
      pmbus_warning_reason_t warn_reason;
      pmbus_decode_warning_status(warning_raw, &warn_reason);
      warn_reason_str = pmbus_get_warning_reason_string(&warn_reason);
    }

    // ── LED status ──
    const bool has_fault = (shutdown_raw != 0);
    const bool has_warning = (warning_raw != 0);
    led_update_power_status(g_pmbus_data.voltage_out_v, has_fault, has_warning);

    // Forward power status to web portal
    {
      pmbus_shutdown_reason_t sd_reason;
      pmbus_decode_shutdown_status(shutdown_raw, &sd_reason);
      wifi_portal_set_power_status(shutdown_raw, warning_raw,
                                   pmbus_get_shutdown_reason_string(&sd_reason),
                                   warn_reason_str);
    }
  }

  static uint32_t start_ms = millis();
  const uint32_t elapsed_seconds = (millis() - start_ms) / 1000U;
  ui_update_work_time(elapsed_seconds);

  ui_warning_task();
  wifi_portal_task();
  espnow_bridge_task();
  ble_server_task();
  beep_task();
  display_task_handler();
}
