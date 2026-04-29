#ifndef PMBUS_H
#define PMBUS_H

#include <Arduino.h>
#include <stdint.h>

// 要更改
static constexpr int kPmbusSclPin = 4;
static constexpr int kPmbusSdaPin = 5;

struct pmbus_data_t
{
	float fan_speed_rpm;
	float temp1_c;
	float temp2_c;
	float power_in_w;
	float power_out_w;
	float current_out_a;
	float current_in_a;
	float voltage_out_v;
	float voltage_in_v;
	float efficiency_percent;
};

struct pmbus_setup_info_t
{
	String spare_part_no;
	String manufacture_date;
	String manufacturer;
	String power_name;
	String option_kit_no;
	String ct_date_codes;
};

void pmbus_init();
uint8_t pmbus_address_7bit();
bool pmbus_probe();
bool pmbus_read_u16(uint8_t reg, uint16_t *value);
bool pmbus_write_u16(uint8_t reg, uint16_t value);

bool pmbus_read_rom_byte(uint8_t addr, uint8_t *value);
String pmbus_get_rom(uint8_t addr, uint8_t len);

String pmbus_get_spn();
String pmbus_get_mfg();
String pmbus_get_mfr();
String pmbus_get_name();
String pmbus_get_okn();
String pmbus_get_ct();
void pmbus_get_setup_info(struct pmbus_setup_info_t *info);

float pmbus_get_input_voltage();
float pmbus_get_input_current();
float pmbus_get_input_power();
float pmbus_get_output_voltage();
float pmbus_get_output_current();
float pmbus_get_output_power();
float pmbus_get_temp1();
float pmbus_get_temp2();
uint16_t pmbus_get_fan_rpm();
uint32_t pmbus_get_run_time();
bool pmbus_set_fan_rpm(uint16_t rpm);
bool pmbus_update_data(struct pmbus_data_t *data);

#endif
