#ifndef BEEP_H
#define BEEP_H

#include <Arduino.h>

void beep_init();
void beep_start(uint32_t frequency_hz = 2000, uint8_t duty = 128);
void beep_stop();
void beep_set_frequency(uint32_t frequency_hz);
void beep_set_duty(uint8_t duty);

#endif
