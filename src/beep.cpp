#include <beep.h>

#include <Preferences.h>

namespace
{
static constexpr uint8_t kBeepPwmChannel = 3;
static constexpr uint8_t kBeepPwmResolutionBits = 8;
static constexpr uint32_t kBeepDefaultFreqHz = 2000;
static constexpr const char *kPrefsNs = "beep_cfg";
static constexpr const char *kPrefsEnabledKey = "enabled";

uint32_t g_beep_frequency_hz = kBeepDefaultFreqHz;
uint8_t g_beep_duty = 0;
bool g_beep_enabled = true;

void beep_apply()
{
  ledcSetup(kBeepPwmChannel, g_beep_frequency_hz, kBeepPwmResolutionBits);
  ledcWrite(kBeepPwmChannel, g_beep_duty);
}

void beep_store_enabled()
{
  Preferences prefs;
  if (prefs.begin(kPrefsNs, false))
  {
    prefs.putBool(kPrefsEnabledKey, g_beep_enabled);
    prefs.end();
  }
}

void beep_load_enabled()
{
  Preferences prefs;
  if (prefs.begin(kPrefsNs, true))
  {
    g_beep_enabled = prefs.getBool(kPrefsEnabledKey, true);
    prefs.end();
  }
}
}

void beep_init()
{
  pinMode(kBeepPin, OUTPUT);
  digitalWrite(kBeepPin, LOW);
  ledcAttachPin(kBeepPin, kBeepPwmChannel);
  beep_load_enabled();
  beep_stop();
}

void beep_start(uint32_t frequency_hz, uint8_t duty)
{
  if (!g_beep_enabled)
  {
    return;
  }

  g_beep_frequency_hz = frequency_hz;
  g_beep_duty = duty;
  beep_apply();
}

void beep_stop()
{
  g_beep_duty = 0;
  beep_apply();
}

void beep_set_frequency(uint32_t frequency_hz)
{
  g_beep_frequency_hz = frequency_hz;
  beep_apply();
}

void beep_set_duty(uint8_t duty)
{
  if (!g_beep_enabled)
  {
    return;
  }

  g_beep_duty = duty;
  beep_apply();
}

void beep_set_enabled(bool enabled)
{
  if (g_beep_enabled == enabled)
  {
    return;
  }

  g_beep_enabled = enabled;
  if (!g_beep_enabled)
  {
    beep_stop();
  }
  beep_store_enabled();
}

bool beep_enabled()
{
  return g_beep_enabled;
}
