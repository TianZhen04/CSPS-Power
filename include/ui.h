#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <lvgl.h>
#include <pmbus.h>

void ui_init();
void ui_update_work_time(uint32_t seconds);
void ui_set_board_temp(float temp_c);
void ui_update_power_data(const pmbus_data_t *data);
lv_group_t *ui_get_input_group();

#endif
