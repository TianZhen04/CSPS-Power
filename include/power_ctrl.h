#ifndef POWER_CTRL_H
#define POWER_CTRL_H

#include <Arduino.h>

void power_ctrl_init();

void power_ctrl_set_software_enabled(bool enabled);
void power_ctrl_set_force_enabled(bool enabled);
void power_ctrl_force_on();
void power_ctrl_force_off();
bool power_ctrl_software_enabled();
bool power_ctrl_force_enabled();
bool power_ctrl_output_enabled();
bool power_ctrl_hw_available();

#endif
