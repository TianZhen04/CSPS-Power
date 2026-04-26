#include <power_ctrl.h>
#include <Preferences.h>

namespace
{
#ifndef POWER_FORCE_SWITCH_PIN
#define POWER_FORCE_SWITCH_PIN 38
#endif

#ifndef POWER_SOFTWARE_SWITCH_PIN
#define POWER_SOFTWARE_SWITCH_PIN 8
#endif

static constexpr uint8_t kPowerForceSwitchPin = POWER_FORCE_SWITCH_PIN;
static constexpr uint8_t kPowerSoftwareSwitchPin = POWER_SOFTWARE_SWITCH_PIN;
static constexpr uint8_t kForceSwitchOnLevel = HIGH;
static constexpr uint8_t kForceSwitchOffLevel = LOW;
static constexpr const char *kPowerPrefsNamespace = "power_cfg";
static constexpr const char *kForceEnabledKey = "force_on";

bool g_software_enabled = false;
bool g_force_enabled = true;
bool g_hw_available = true;
Preferences g_power_prefs;
bool g_power_prefs_ready = false;

bool pin_reserved_by_memory_bus(uint8_t pin)
{
#if defined(ARDUINO_ESP32_S3R8N16) || defined(BOARD_HAS_PSRAM)
  return pin >= 33U && pin <= 37U;
#else
  (void)pin;
  return false;
#endif
}

void apply_output_state()
{
  if (!g_hw_available)
  {
    return;
  }

  // PS_ON is active low. LOW pulls the open-drain output down to enable power,
  // HIGH releases the line and lets the external pull-up turn power off.
  const bool output_on = g_software_enabled && g_force_enabled;
  digitalWrite(kPowerSoftwareSwitchPin, output_on ? LOW : HIGH);
}

void apply_force_state()
{
  if (!g_hw_available)
  {
    return;
  }

  digitalWrite(kPowerForceSwitchPin, g_force_enabled ? kForceSwitchOnLevel : kForceSwitchOffLevel);
}

void load_force_state()
{
  g_power_prefs_ready = g_power_prefs.begin(kPowerPrefsNamespace, false);
  if (!g_power_prefs_ready)
  {
    g_force_enabled = true;
    return;
  }

  g_force_enabled = g_power_prefs.getBool(kForceEnabledKey, true);
}

void save_force_state()
{
  if (!g_power_prefs_ready)
  {
    return;
  }

  g_power_prefs.putBool(kForceEnabledKey, g_force_enabled);
}
}

void power_ctrl_init()
{
  load_force_state();
  g_software_enabled = false;

  if (pin_reserved_by_memory_bus(kPowerForceSwitchPin) ||
      pin_reserved_by_memory_bus(kPowerSoftwareSwitchPin))
  {
    g_hw_available = false;
    Serial.printf("power control disabled: GPIO%u/GPIO%u reserved by flash/psram bus on this ESP32-S3 target\n",
                  static_cast<unsigned>(kPowerSoftwareSwitchPin),
                  static_cast<unsigned>(kPowerForceSwitchPin));
    return;
  }

  pinMode(kPowerForceSwitchPin, OUTPUT);
  apply_force_state();

  pinMode(kPowerSoftwareSwitchPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(kPowerSoftwareSwitchPin, HIGH);
  apply_output_state();
}

void power_ctrl_set_software_enabled(bool enabled)
{
  g_software_enabled = enabled;
  apply_output_state();
}

void power_ctrl_set_force_enabled(bool enabled)
{
  const bool changed = (g_force_enabled != enabled);
  g_force_enabled = enabled;
  if (!g_force_enabled)
  {
    g_software_enabled = false;
  }
  if (changed)
  {
    save_force_state();
  }
  apply_force_state();
  apply_output_state();
}

void power_ctrl_force_on()
{
  power_ctrl_set_force_enabled(true);
}

void power_ctrl_force_off()
{
  power_ctrl_set_force_enabled(false);
}

bool power_ctrl_software_enabled()
{
  return g_software_enabled;
}

bool power_ctrl_force_enabled()
{
  return g_force_enabled;
}

bool power_ctrl_output_enabled()
{
  return g_software_enabled && g_force_enabled;
}

bool power_ctrl_hw_available()
{
  return g_hw_available;
}
