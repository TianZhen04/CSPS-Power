#include <math.h>
#include <ntc_temp.h>

namespace
{
static constexpr uint8_t kNtcSampleCount = 8;
}

void ntc_temp_init()
{
  pinMode(kNtcAdcPin, INPUT);
#if defined(ARDUINO_ARCH_ESP32)
  analogReadResolution(12);
  analogSetPinAttenuation(kNtcAdcPin, ADC_11db);
#endif
}

bool ntc_temp_read_c(float *temp_c)
{
  if (temp_c == nullptr)
  {
    return false;
  }

  uint32_t mv_sum = 0;
  for (uint8_t i = 0; i < kNtcSampleCount; ++i)
  {
    mv_sum += static_cast<uint32_t>(analogReadMilliVolts(kNtcAdcPin));
    delayMicroseconds(300);
  }

  const uint32_t mv = mv_sum / kNtcSampleCount;
  if (mv == 0U || mv >= kNtcVccMilliVolts)
  {
    return false;
  }

  const float v_ratio = static_cast<float>(mv) / static_cast<float>(kNtcVccMilliVolts);
  const float r_ntc = kNtcSeriesResistorOhm * (v_ratio / (1.0f - v_ratio));
  if (r_ntc <= 0.0f)
  {
    return false;
  }

  const float t0_k = kNtcNominalTempC + 273.15f;
  const float inv_t = (1.0f / t0_k) + (logf(r_ntc / kNtcNominalResistorOhm) / kNtcBeta);
  if (inv_t <= 0.0f)
  {
    return false;
  }

  *temp_c = (1.0f / inv_t) - 273.15f;
  return true;
}
