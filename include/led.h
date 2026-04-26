#ifndef LED_H
#define LED_H

#include <Arduino.h>

void led_init();
void led_set_brightness(uint8_t led_index, uint8_t brightness);
void led_set_all_brightness(uint8_t brightness);
uint8_t led_get_brightness(uint8_t led_index);

#endif
