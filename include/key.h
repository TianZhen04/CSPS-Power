#ifndef _KEY_H
#define _KEY_H

#include <Arduino.h>
#include <lvgl.h>

#define SWITCH_LEFT 4
#define SWITCH_RIGHT 6
#define SWITCH_ENTER 5

void key_init();
lv_indev_t* get_keypad_indev();

#endif