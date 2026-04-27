#ifndef LED_H
#define LED_H

#include <Arduino.h>

struct LedPwmConfig
{
  uint8_t pin;
  uint8_t channel;
};

static constexpr LedPwmConfig kLedConfigs[] = {
  {15, 1},//红
  {16, 2},//绿
};

void led_init();
void led_set_brightness(uint8_t led_index, uint8_t brightness);
void led_set_all_brightness(uint8_t brightness);
uint8_t led_get_brightness(uint8_t led_index);

#endif
