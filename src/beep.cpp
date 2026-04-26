#include <beep.h>

namespace
{
static constexpr uint8_t kBeepPin = 17;
static constexpr uint8_t kBeepPwmChannel = 3;
static constexpr uint8_t kBeepPwmResolutionBits = 8;
static constexpr uint32_t kBeepDefaultFreqHz = 2000;

uint32_t g_beep_frequency_hz = kBeepDefaultFreqHz;
uint8_t g_beep_duty = 0;

void beep_apply()
{
  ledcSetup(kBeepPwmChannel, g_beep_frequency_hz, kBeepPwmResolutionBits);
  ledcWrite(kBeepPwmChannel, g_beep_duty);
}
}

void beep_init()
{
  pinMode(kBeepPin, OUTPUT);
  digitalWrite(kBeepPin, LOW);
  ledcAttachPin(kBeepPin, kBeepPwmChannel);
  beep_stop();
}

void beep_start(uint32_t frequency_hz, uint8_t duty)
{
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
  g_beep_duty = duty;
  beep_apply();
}
