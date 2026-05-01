#include <led.h>

namespace
{
static constexpr uint32_t kLedPwmFreqHz = 5000;
static constexpr uint8_t kLedPwmResolutionBits = 8;

uint8_t g_led_brightness[sizeof(kLedConfigs) / sizeof(kLedConfigs[0])] = {};

bool is_valid_led_index(uint8_t led_index)
{
  return led_index < (sizeof(kLedConfigs) / sizeof(kLedConfigs[0]));
}
}

void led_init()
{
  for (uint8_t i = 0; i < (sizeof(kLedConfigs) / sizeof(kLedConfigs[0])); ++i)
  {
    pinMode(kLedConfigs[i].pin, OUTPUT);
    digitalWrite(kLedConfigs[i].pin, LOW);
    ledcSetup(kLedConfigs[i].channel, kLedPwmFreqHz, kLedPwmResolutionBits);
    ledcAttachPin(kLedConfigs[i].pin, kLedConfigs[i].channel);
    ledcWrite(kLedConfigs[i].channel, 0);
    g_led_brightness[i] = 0;
  }
}

void led_set_brightness(uint8_t led_index, uint8_t brightness)
{
  if (!is_valid_led_index(led_index))
  {
    return;
  }

  g_led_brightness[led_index] = brightness;
  ledcWrite(kLedConfigs[led_index].channel, brightness);
}

void led_set_all_brightness(uint8_t brightness)
{
  for (uint8_t i = 0; i < (sizeof(kLedConfigs) / sizeof(kLedConfigs[0])); ++i)
  {
    led_set_brightness(i, brightness);
  }
}

uint8_t led_get_brightness(uint8_t led_index)
{
  if (!is_valid_led_index(led_index))
  {
    return 0;
  }

  return g_led_brightness[led_index];
}

void led_set_red(bool on)
{
  led_set_brightness(0, on ? 255 : 0);
}

void led_set_green(bool on)
{
  led_set_brightness(1, on ? 255 : 0);
}

void led_update_power_status(float output_voltage_v, bool has_fault, bool has_warning)
{
  // Fault / warning takes priority — red LED
  if (has_fault || has_warning)
  {
    led_set_red(true);
    led_set_green(false);
    return;
  }

  // Output voltage in normal range 11.5V – 12.5V → green
  if (output_voltage_v >= 11.5f && output_voltage_v <= 12.5f)
  {
    led_set_red(false);
    led_set_green(true);
    return;
  }

  // Output absent or out of range → both off
  led_set_red(false);
  led_set_green(false);
}
