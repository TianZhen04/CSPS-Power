#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

void display_init();
void display_task_handler();
void backlight_set(uint8_t brightness);
void display_set_brightness(uint8_t brightness);
uint8_t display_get_brightness();
void display_set_rotation(uint8_t rotation);
uint8_t display_get_rotation();

#endif
