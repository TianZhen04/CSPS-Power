#ifndef POWER_CTRL_H
#define POWER_CTRL_H

#include <Arduino.h>

#ifndef POWER_FORCE_SWITCH_PIN
#define POWER_FORCE_SWITCH_PIN 38
#endif

#ifndef POWER_SOFTWARE_SWITCH_PIN
#define POWER_SOFTWARE_SWITCH_PIN 8
#endif

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
