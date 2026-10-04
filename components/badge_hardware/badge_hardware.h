#pragma once
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/motion/motion_component.h"
#include "bme69x.h"
#include <string>
#include <array>

namespace esphome::badge_hardware {
class BadgeHardware : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void mount_sd();
  std::string directory() const;
  std::string summary() const;
  void reset_gas_baseline();
  void set_motion(motion::MotionComponent *v) { motion_=v; }
  void set_temperature(sensor::Sensor *v) { temperature_=v; }
  void set_humidity(sensor::Sensor *v) { humidity_=v; }
  void set_pressure(sensor::Sensor *v) { pressure_=v; }
  void set_gas_resistance(sensor::Sensor *v) { gas_=v; }
  void set_gas_response(sensor::Sensor *v) { gas_response_=v; }
  void set_gas_level(text_sensor::TextSensor *v) { gas_level_sensor_=v; }
  void set_gas_trend(text_sensor::TextSensor *v) { gas_trend_sensor_=v; }
  void set_sd_capacity(sensor::Sensor *v) { sd_capacity_=v; }
  void set_sd_mounted(binary_sensor::BinarySensor *v) { sd_mounted_=v; }
  void set_sd_status(text_sensor::TextSensor *v) { sd_status_sensor_=v; }
 protected:
  static int8_t read_(uint8_t reg, uint8_t *data, uint32_t len, void *ctx);
  static int8_t write_(uint8_t reg, const uint8_t *data, uint32_t len, void *ctx);
  static void delay_(uint32_t us, void *ctx);
  void finish_measurement_();
  bme69x_dev dev_{};
  bme69x_conf conf_{};
  motion::MotionComponent *motion_{nullptr};
  motion::MotionData motion_data_{};
  bool motion_ready_{false};
  bool bme_ready_{false}, pending_{false}, mounted_{false};
  sensor::Sensor *temperature_{nullptr}, *humidity_{nullptr}, *pressure_{nullptr}, *gas_{nullptr}, *gas_response_{nullptr}, *sd_capacity_{nullptr};
  uint32_t gas_started_{0}, baseline_samples_{0};
  bool gas_started_valid_{false};
  double baseline_log_sum_{0};
  float gas_baseline_{0};
  struct GasSample { uint32_t time; float resistance; };
  std::array<GasSample,8> gas_history_{};
  uint8_t gas_history_pos_{0}, gas_history_count_{0};
  std::string gas_level_{"Learning"}, gas_trend_{"Waiting"};
  text_sensor::TextSensor *gas_level_sensor_{nullptr}, *gas_trend_sensor_{nullptr};
  binary_sensor::BinarySensor *sd_mounted_{nullptr};
  text_sensor::TextSensor *sd_status_sensor_{nullptr};
  std::string sd_status_{"Not probed"};
};
}
