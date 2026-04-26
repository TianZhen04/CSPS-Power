#include "KCORES_CSPS.h"
#include "esphome.h"

CSPS PowerSupply(0x5F, 0x57);

class CSPSPower : public PollingComponent {
  public:
    Sensor *fan_speed = new Sensor();
    Sensor *temp1 = new Sensor();
    Sensor *temp2 = new Sensor();
    Sensor *power_in = new Sensor();
    Sensor *power_out = new Sensor();
    Sensor *current_out = new Sensor();
    Sensor *current_in = new Sensor();
    Sensor *voltage_out = new Sensor();
    Sensor *voltage_in = new Sensor();
    Sensor *efficiency = new Sensor();

    CSPSPower(): PollingComponent(1000) { }
    // 这个3000大概指的是3000毫秒更新一次信息吧

    void setup() override {
      Wire.setClock(100000);

      ESP_LOGD("Power Supply", "Spare Part No: %s", PowerSupply.getSPN().c_str());
      ESP_LOGD("Power Supply", "Manufacture Date: %s", PowerSupply.getMFG().c_str());
      ESP_LOGD("Power Supply", "Manufacturer: %s", PowerSupply.getMFR().c_str());
      ESP_LOGD("Power Supply", "Power Name: %s", PowerSupply.getName().c_str());
      ESP_LOGD("Power Supply", "Option Kit No: %s", PowerSupply.getOKN().c_str());
      ESP_LOGD("Power Supply", "CT Date Codes: %s", PowerSupply.getCT().c_str());
    }

    void update() override {
      // float current_out = PowerSupply.getOutputCurrent() / 256 / 256 / 256;

      // 使用读取到的输出电压电流计算输出功率，我想显示到小数点后两位
      float power_out_value = PowerSupply.getOutputCurrent() * PowerSupply.getOutputVoltage();
      
      // 使用读取到的输入电压电流计算输入功率，我想显示到小数点后两位，似乎这个计算出来的准一点
      //float power_in_value = PowerSupply.getInputCurrent() * PowerSupply.getInputVoltage();

      // 使用输出输入的功率计算转化效率，如果用待机12v给路由器供电，计算的效率会低一点
      float efficiency_value = (power_out_value / PowerSupply.getInputPower()) * 100;
    
      fan_speed->publish_state(PowerSupply.getFanRPM());
      temp1->publish_state(PowerSupply.getTemp1());
      temp2->publish_state(PowerSupply.getTemp2());

      // 这是直接读取到的输出功率，整数
      // power_out->publish_state(PowerSupply.getOutputPower());
      
      // 这是直接读取到的输入功率，空载好像直接显示20W，整数
      power_in->publish_state(PowerSupply.getInputPower());

      power_out->publish_state(power_out_value);
      //power_in->publish_state(power_in_value);
      current_out->publish_state(PowerSupply.getOutputCurrent());
      current_in->publish_state(PowerSupply.getInputCurrent());
      voltage_out->publish_state(PowerSupply.getOutputVoltage());
      voltage_in->publish_state(PowerSupply.getInputVoltage());
      efficiency->publish_state(efficiency_value);
    }
};
