#ifndef NTC_TEMP_H
#define NTC_TEMP_H

#include <Arduino.h>

static constexpr uint8_t kNtcAdcPin = 1;
static constexpr float kNtcSeriesResistorOhm = 100000.0f;   // 3.3V -> 100k -> ADC node
static constexpr float kNtcNominalResistorOhm = 100000.0f; // NTC resistance at 25C
static constexpr float kNtcBeta = 3950.0f;
static constexpr float kNtcNominalTempC = 25.0f;
static constexpr uint32_t kNtcVccMilliVolts = 3300U;

void ntc_temp_init();
bool ntc_temp_read_c(float *temp_c);

#endif
