#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "input_core.h"
#include "esphome/components/number/number.h"
#include "esphome/components/badge_hardware/badge_hardware.h"
#include "esphome/components/rtttl/rtttl.h"
#include "esphome/components/api/custom_api_device.h"
#include <vector>
#include <lvgl.h>

namespace esphome::badge_keyboard {

class BadgeKeyboard : public PollingComponent, public i2c::I2CDevice, public api::CustomAPIDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { // C6 EN also resets the keypad; configure it after hosted Wi-Fi startup.
    return setup_priority::AFTER_WIFI; }
  void start_ui();
  void sync_lights(std::string payload);
  void set_brightness(float value);
  void set_keyboard_brightness(float value);
  void set_brightness_number(number::Number *n) { brightness_number_=n; }
  void set_keyboard_brightness_number(number::Number *n) { keyboard_brightness_number_=n; }
  void set_hardware(badge_hardware::BadgeHardware *v) { hardware_=v; }
  void set_audio(rtttl::Rtttl *v) { audio_=v; }
 private:
  badge_hardware::BadgeHardware *hardware_{nullptr};
  rtttl::Rtttl *audio_{nullptr};
  struct HouseLight {
    std::string id, name, area, state;
    int brightness{0};
    bool dimmable{false};
    bool pending{false}, desired_on{false};
    int desired_brightness{-1};
    uint32_t sent_at{0};
  };
  std::vector<HouseLight> lights_;
  std::vector<size_t> filtered_lights_;
  std::string light_filter_, light_message_;
  size_t light_selected_{0};
  uint32_t lights_received_{0}, lights_refresh_{0}, discovery_at_{0}, light_message_at_{0}, light_dim_at_{0};
  bool catalog_received_{false}, ha_was_connected_{false};
  unsigned lights_total_{0};
  lv_obj_t *light_rows_[8]{}, *light_names_[8]{}, *light_states_[8]{}, *light_detail_{nullptr};
  void filter_lights_(const std::string &selected_id = "");
  void lights_ui_();
  void lights_key_(uint8_t key, char c, bool repeating);
  void light_action_(bool toggle, int step);
  void request_lights_();
  bool lights_online_() const;
  void set_light_message_(const std::string &text);
  void diagnostics_ui_(uint8_t key = 0, bool pressed = false);
  uint32_t sensor_refresh_{0};
  lv_obj_t *dock_[6]{};
  uint8_t brightness_{100}, keyboard_brightness_{20};
  bool brightness_dirty_{true}, keyboard_brightness_dirty_{true};
  uint32_t brightness_retry_{0};
  number::Number *brightness_number_{nullptr}, *keyboard_brightness_number_{nullptr};
  Keys keys_;
  CommandLine line_;
  bool ready_{false};
  uint8_t active_{0}, repeat_key_{0};
  uint32_t repeat_at_{0}, errors_{0}, events_{0};
  lv_obj_t *window_{nullptr}, *title_{nullptr}, *output_{nullptr}, *input_{nullptr};
  lv_obj_t *body_{nullptr}, *status_{nullptr};
  std::string transcript_;
  CommandLine wifi_ssid_, wifi_password_;
  uint8_t wifi_field_{0};
  uint32_t network_refresh_{0};
  lv_obj_t *ssid_input_{nullptr}, *password_input_{nullptr}, *network_status_{nullptr};
  void network_ui_();
  void network_key_(uint8_t key, char c);
  void open_app_(uint8_t app);
  void key_(uint8_t key, bool repeating=false);
  void sync_line_();
  void print_(const std::string &text);
  void execute_(const std::string &command);
  void fault_();
  static const char *app_name_(uint8_t app);
};

}  // namespace esphome::badge_keyboard
