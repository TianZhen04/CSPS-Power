#ifndef _KEY_H
#define _KEY_H

#include <Arduino.h>
#include <lvgl.h>

#define SWITCH_LEFT 39
#define SWITCH_RIGHT 41
#define SWITCH_ENTER 40

void key_init();
lv_indev_t* get_keypad_indev();

#endif