#include <Arduino.h>
#include <Wire.h>
#include <pmbus.h>

static constexpr uint32_t kPmbusI2cFreqHz = 100000;
static constexpr uint8_t kPmbusAddr8Bit = 0xBE;
static constexpr uint8_t kPmbusAddr7Bit = static_cast<uint8_t>(kPmbusAddr8Bit >> 1);
static constexpr uint8_t kPmbusRomAddr7Bit = static_cast<uint8_t>(kPmbusAddr7Bit - 0x08);

static uint8_t twos_complement_checksum(uint16_t sum)
{
  return static_cast<uint8_t>((0x100 - (sum & 0xFFU)) & 0xFFU);
}

static bool pmbus_read_u16_with_csps_checksum(uint8_t reg, uint16_t *value)
{
  const uint8_t command_cs = twos_complement_checksum(static_cast<uint16_t>(reg) +
                                                      static_cast<uint16_t>(kPmbusAddr7Bit << 1));

  Wire.beginTransmission(kPmbusAddr7Bit);
  Wire.write(reg);
  Wire.write(command_cs);
  if (Wire.endTransmission() != 0)
  {
    return false;
  }

  const uint8_t expected = 3;
  const uint8_t got = Wire.requestFrom(static_cast<int>(kPmbusAddr7Bit), static_cast<int>(expected));
  if (got != expected)
  {
    return false;
  }

  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  const uint8_t data_cs = Wire.read();
  const uint8_t calc_cs = twos_complement_checksum(static_cast<uint16_t>(lo) + static_cast<uint16_t>(hi));

  if (calc_cs != data_cs)
  {
    return false;
  }

  *value = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
  return true;
}

static bool pmbus_read_u16_standard(uint8_t reg, uint16_t *value)
{
  Wire.beginTransmission(kPmbusAddr7Bit);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0)
  {
    return false;
  }

  const uint8_t expected = 2;
  const uint8_t got = Wire.requestFrom(static_cast<int>(kPmbusAddr7Bit), static_cast<int>(expected));
  if (got != expected)
  {
    return false;
  }

  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *value = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
  return true;
}

uint8_t pmbus_address_7bit()
{
  return kPmbusAddr7Bit;
}

bool pmbus_probe()
{
  Wire.beginTransmission(kPmbusAddr7Bit);
  return Wire.endTransmission() == 0;
}

static bool pmbus_write_u16_csps(uint8_t reg, uint16_t value)
{
  const uint8_t lo = static_cast<uint8_t>(value & 0xFFU);
  const uint8_t hi = static_cast<uint8_t>((value >> 8) & 0xFFU);
  const uint16_t sum = static_cast<uint16_t>(kPmbusAddr7Bit << 1) +
                       static_cast<uint16_t>(reg) +
                       static_cast<uint16_t>(lo) +
                       static_cast<uint16_t>(hi);
  const uint8_t reg_cs = twos_complement_checksum(sum);

  Wire.beginTransmission(kPmbusAddr7Bit);
  Wire.write(reg);
  Wire.write(lo);
  Wire.write(hi);
  Wire.write(reg_cs);
  return Wire.endTransmission() == 0;
}

bool pmbus_write_u16(uint8_t reg, uint16_t value)
{
  return pmbus_write_u16_csps(reg, value);
}

bool pmbus_read_u16(uint8_t reg, uint16_t *value)
{
  if (value == nullptr)
  {
    return false;
  }

  // CSPS implementation uses command checksum + 3-byte response.
  // Keep a 2-byte fallback for compatibility with plain PMBus devices.
  if (pmbus_read_u16_with_csps_checksum(reg, value))
  {
    return true;
  }

  return pmbus_read_u16_standard(reg, value);
}

bool pmbus_read_rom_byte(uint8_t addr, uint8_t *value)
{
  if (value == nullptr)
  {
    return false;
  }

  Wire.beginTransmission(kPmbusRomAddr7Bit);
  Wire.write(addr);
  if (Wire.endTransmission() != 0)
  {
    return false;
  }

  const uint8_t expected = 1;
  const uint8_t got = Wire.requestFrom(static_cast<int>(kPmbusRomAddr7Bit), static_cast<int>(expected));
  if (got != expected || Wire.available() < 1)
  {
    return false;
  }

  *value = Wire.read();
  return true;
}

String pmbus_get_rom(uint8_t addr, uint8_t len)
{
  String out;
  out.reserve(len);

  for (uint8_t i = 0; i < len; ++i)
  {
    uint8_t value = 0;
    if (!pmbus_read_rom_byte(static_cast<uint8_t>(addr + i), &value))
    {
      break;
    }
    out.concat(static_cast<char>(value));
  }

  return out;
}

String pmbus_get_spn()
{
  return pmbus_get_rom(0x12, 0x0A);
}

String pmbus_get_mfg()
{
  return pmbus_get_rom(0x1D, 0x08);
}

String pmbus_get_mfr()
{
  return pmbus_get_rom(0x2C, 0x05);
}

String pmbus_get_name()
{
  return pmbus_get_rom(0x32, 0x1A);
}

String pmbus_get_okn()
{
  return pmbus_get_rom(0x4D, 0x0A);
}

String pmbus_get_ct()
{
  return pmbus_get_rom(0x5B, 0x0E);
}

void pmbus_get_setup_info(struct pmbus_setup_info_t *info)
{
  if (info == nullptr)
  {
    return;
  }

  info->spare_part_no = pmbus_get_spn();
  info->manufacture_date = pmbus_get_mfg();
  info->manufacturer = pmbus_get_mfr();
  info->power_name = pmbus_get_name();
  info->option_kit_no = pmbus_get_okn();
  info->ct_date_codes = pmbus_get_ct();
}

static uint16_t pmbus_read_u16_or_zero(uint8_t reg)
{
  uint16_t value = 0;
  if (!pmbus_read_u16(reg, &value))
  {
    return 0;
  }
  return value;
}

float pmbus_get_input_voltage()
{
  return static_cast<float>(pmbus_read_u16_or_zero(0x08)) / 32.0f;
}

float pmbus_get_input_current()
{
  return static_cast<float>(pmbus_read_u16_or_zero(0x0A)) / 64.0f;
}

float pmbus_get_input_power()
{
  return static_cast<float>(pmbus_read_u16_or_zero(0x0C));
}

float pmbus_get_output_voltage()
{
  return static_cast<float>(pmbus_read_u16_or_zero(0x0E)) / 256.0f;
}

float pmbus_get_output_current()
{
  return static_cast<float>(pmbus_read_u16_or_zero(0x10)) / 64.0f;
}

float pmbus_get_output_power()
{
  return static_cast<float>(pmbus_read_u16_or_zero(0x12));
}

float pmbus_get_temp1()
{
  return static_cast<float>(pmbus_read_u16_or_zero(0x1A)) / 64.0f;
}

float pmbus_get_temp2()
{
  return static_cast<float>(pmbus_read_u16_or_zero(0x1C)) / 64.0f;
}

uint16_t pmbus_get_fan_rpm()
{
  return pmbus_read_u16_or_zero(0x1E);
}

uint32_t pmbus_get_run_time()
{
  return static_cast<uint32_t>(pmbus_read_u16_or_zero(0x30));
}

bool pmbus_set_fan_rpm(uint16_t rpm)
{
  return pmbus_write_u16(0x40, rpm);
}

bool pmbus_update_data(struct pmbus_data_t *data)
{
  if (data == nullptr)
  {
    return false;
  }

  // Fast fail when bus/device is absent to avoid repeated I2C retries.
  if (!pmbus_probe())
  {
    *data = pmbus_data_t{};
    return false;
  }

  uint16_t raw = 0;
  bool any_valid = false;

  if (pmbus_read_u16(0x1E, &raw))
  {
    data->fan_speed_rpm = static_cast<float>(raw);
    any_valid = true;
  }
  else
  {
    data->fan_speed_rpm = 0.0f;
  }

  if (pmbus_read_u16(0x1A, &raw))
  {
    data->temp1_c = static_cast<float>(raw) / 64.0f;
    any_valid = true;
  }
  else
  {
    data->temp1_c = 0.0f;
  }

  if (pmbus_read_u16(0x1C, &raw))
  {
    data->temp2_c = static_cast<float>(raw) / 64.0f;
    any_valid = true;
  }
  else
  {
    data->temp2_c = 0.0f;
  }

  if (pmbus_read_u16(0x0C, &raw))
  {
    data->power_in_w = static_cast<float>(raw);
    any_valid = true;
  }
  else
  {
    data->power_in_w = 0.0f;
  }

  if (pmbus_read_u16(0x10, &raw))
  {
    data->current_out_a = static_cast<float>(raw) / 64.0f;
    any_valid = true;
  }
  else
  {
    data->current_out_a = 0.0f;
  }

  if (pmbus_read_u16(0x0A, &raw))
  {
    data->current_in_a = static_cast<float>(raw) / 64.0f;
    any_valid = true;
  }
  else
  {
    data->current_in_a = 0.0f;
  }

  if (pmbus_read_u16(0x0E, &raw))
  {
    data->voltage_out_v = static_cast<float>(raw) / 256.0f;
    any_valid = true;
  }
  else
  {
    data->voltage_out_v = 0.0f;
  }

  if (pmbus_read_u16(0x08, &raw))
  {
    data->voltage_in_v = static_cast<float>(raw) / 32.0f;
    any_valid = true;
  }
  else
  {
    data->voltage_in_v = 0.0f;
  }

  // Keep the same behavior as CSPS main.hpp update: Vout * Iout.
  data->power_out_w = data->current_out_a * data->voltage_out_v;
  if (data->power_in_w > 0.001f)
  {
    data->efficiency_percent = (data->power_out_w / data->power_in_w) * 100.0f;
  }
  else
  {
    data->efficiency_percent = 0.0f;
  }

  return any_valid;
}

void pmbus_init()
{
  Wire.begin(kPmbusSdaPin, kPmbusSclPin, kPmbusI2cFreqHz);
  Wire.setTimeOut(20);
  Serial.printf("PMBus I2C initialized: SDA=IO%d, SCL=IO%d, Freq=%luHz, DevAddr8=0x%02X, DevAddr7=0x%02X\n",
                kPmbusSdaPin,
                kPmbusSclPin,
                static_cast<unsigned long>(kPmbusI2cFreqHz),
                kPmbusAddr8Bit,
                kPmbusAddr7Bit);
  Serial.printf("PMBus ROM addr7=0x%02X\n", kPmbusRomAddr7Bit);

  Serial.printf("PMBus probe: %s\n", pmbus_probe() ? "OK" : "FAIL");

  uint16_t uc_info = 0;
  if (pmbus_read_u16(0x00, &uc_info))
  {
    Serial.printf("PMBus reg[0x00] uC Info = 0x%04X\n", uc_info);
  }
  else
  {
    Serial.println("PMBus read reg[0x00] failed");
  }
}
