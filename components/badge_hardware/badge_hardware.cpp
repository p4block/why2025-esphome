#include "badge_hardware.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include <dirent.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace esphome::badge_hardware {
static const char *const TAG="badge_hardware";
int8_t BadgeHardware::read_(uint8_t reg,uint8_t *data,uint32_t len,void *ctx) {
  return static_cast<BadgeHardware *>(ctx)->read_register(reg,data,len)==i2c::ERROR_OK ? 0 : -1;
}
int8_t BadgeHardware::write_(uint8_t reg,const uint8_t *data,uint32_t len,void *ctx) {
  return static_cast<BadgeHardware *>(ctx)->write_register(reg,data,len)==i2c::ERROR_OK ? 0 : -1;
}
void BadgeHardware::delay_(uint32_t us,void *) { delay_microseconds_safe(us); }
void BadgeHardware::setup() {
  motion_->add_listener([this](motion::MotionData &data) { motion_data_=data; motion_ready_=true; });
  dev_.intf=BME69X_I2C_INTF; dev_.intf_ptr=this; dev_.read=read_; dev_.write=write_; dev_.delay_us=delay_; dev_.amb_temp=25;
  auto err=bme69x_init(&dev_);
  if (err==BME69X_OK) {
    conf_.filter=BME69X_FILTER_SIZE_3; conf_.odr=BME69X_ODR_NONE;
    conf_.os_hum=BME69X_OS_2X; conf_.os_pres=BME69X_OS_4X; conf_.os_temp=BME69X_OS_2X;
    err=bme69x_set_conf(&conf_,&dev_);
  }
  if (err==BME69X_OK) {
    bme69x_heatr_conf heater{}; heater.enable=BME69X_ENABLE; heater.heatr_temp=300; heater.heatr_dur=100;
    err=bme69x_set_heatr_conf(BME69X_FORCED_MODE,&heater,&dev_);
  }
  bme_ready_=err==BME69X_OK;
  if (bme_ready_) ESP_LOGI(TAG,"BME690 initialized: chip 0x%02X, variant %u",dev_.chip_id,(unsigned)dev_.variant_id);
  else ESP_LOGE(TAG,"BME690 initialization failed: %d",err);
  // Probe once after boot; no hotplug polling or automatic formatting.
  this->set_timeout("sd-probe",2000,[this]() { mount_sd(); });
  gas_level_sensor_->publish_state(gas_level_); gas_trend_sensor_->publish_state(gas_trend_);
}
void BadgeHardware::update() {
  if (!bme_ready_ || pending_) return;
  auto err=bme69x_set_op_mode(BME69X_FORCED_MODE,&dev_);
  if (err!=BME69X_OK) { this->status_set_warning(); return; }
  pending_=true;
  auto ms=(bme69x_get_meas_dur(BME69X_FORCED_MODE,&conf_,&dev_)+100000+999)/1000;
  this->set_timeout("bme-measurement",ms,[this]() { finish_measurement_(); });
}
void BadgeHardware::finish_measurement_() {
  pending_=false;
  bme69x_data data{}; uint8_t count=0;
  auto err=bme69x_get_data(BME69X_FORCED_MODE,&data,&count,&dev_);
  if (err!=BME69X_OK || !count) { this->status_set_warning(); return; }
  this->status_clear_warning();
  temperature_->publish_state(data.temperature); humidity_->publish_state(data.humidity); pressure_->publish_state(data.pressure/100.0f);
  if (std::isfinite(data.gas_resistance)) gas_->publish_state(data.gas_resistance);
  uint8_t raw_gas[2]{};
  const bool raw_ok=this->read_bytes(0x2c,raw_gas,sizeof(raw_gas));
  const uint16_t gas_adc=(static_cast<uint16_t>(raw_gas[0])<<2)|(raw_gas[1]>>6);
  const bool saturated=raw_ok && (gas_adc==0 || gas_adc==1023);
  if (saturated) {
    gas_level_="Out of range"; gas_trend_="Unavailable";
    gas_level_sensor_->publish_state(gas_level_); gas_trend_sensor_->publish_state(gas_trend_);
    gas_response_->publish_state(NAN);
    ESP_LOGW(TAG,"Gas ADC saturated: %u, range %u; relative classification unavailable",gas_adc,raw_gas[1]&15);
  }
  if (!saturated && raw_ok && (data.status & (BME69X_GASM_VALID_MSK | BME69X_HEAT_STAB_MSK))==(BME69X_GASM_VALID_MSK | BME69X_HEAT_STAB_MSK) && std::isfinite(data.gas_resistance) && data.gas_resistance>0) {
    if (gas_baseline_==0) { gas_level_="Learning"; gas_level_sensor_->publish_state(gas_level_); }
    if (!gas_started_valid_) { gas_started_=millis(); gas_started_valid_=true; }
    const uint32_t elapsed=millis()-gas_started_;
    uint32_t closest_age=UINT32_MAX; float previous=0;
    for (uint8_t i=0;i<gas_history_count_;++i) {
      uint32_t age=millis()-gas_history_[i].time;
      if (age>=60000 && age<=90000 && age<closest_age) { closest_age=age; previous=gas_history_[i].resistance; }
    }
    gas_history_[gas_history_pos_]={millis(),data.gas_resistance};
    gas_history_pos_=(gas_history_pos_+1)%gas_history_.size();
    gas_history_count_=std::min<size_t>(gas_history_count_+1,gas_history_.size());
    if (previous>0) {
      // Conductance trend over approximately one minute, with a 5% deadband.
      float change=100.0f*(previous/data.gas_resistance-1.0f);
      gas_trend_=change>5 ? "Increasing" : change<-5 ? "Decreasing" : "Stable";
      gas_trend_sensor_->publish_state(gas_trend_);
    }
    // Relative conductance only, not VOC concentration, IAQ, or health grading.
    // Let the heater settle for four minutes; average the next minute in log space.
    if (gas_baseline_==0 && elapsed>=240000) {
      baseline_log_sum_+=std::log(data.gas_resistance); ++baseline_samples_;
      if (elapsed>=300000 && baseline_samples_>=6) {
        gas_baseline_=std::exp(baseline_log_sum_/baseline_samples_);
        ESP_LOGI(TAG,"Relative gas baseline learned from %u samples",(unsigned)baseline_samples_);
      }
    }
    if (gas_baseline_>0) {
      const float response=100.0f*(gas_baseline_/data.gas_resistance-1.0f);
      gas_response_->publish_state(response);
      // These thresholds describe relative response, never absolute VOC or IAQ.
      gas_level_=response<=20 ? "Low" : response<=100 ? "Mid" : "High";
      gas_level_sensor_->publish_state(gas_level_);
      ESP_LOGD(TAG,"Relative gas: %s, %+.1f%% versus startup baseline; past minute %s",gas_level_.c_str(),response,gas_trend_.c_str());
    }
  }
  dev_.amb_temp=static_cast<int8_t>(data.temperature);
  ESP_LOGD(TAG,"Telemetry: %.2f C, %.1f %%, %.1f hPa, gas %.0f ohm [0x%02X]; accel %.3f %.3f %.3f g, gyro %.1f %.1f %.1f deg/s",
           data.temperature,data.humidity,data.pressure/100.0f,data.gas_resistance,data.status,
           motion_data_.acceleration[0],motion_data_.acceleration[1],motion_data_.acceleration[2],
           motion_data_.angular_rate[0],motion_data_.angular_rate[1],motion_data_.angular_rate[2]);
}
void BadgeHardware::mount_sd() {
  if (mounted_) return;
  sdmmc_host_t host=SDMMC_HOST_DEFAULT(); host.slot=SDMMC_HOST_SLOT_0; host.max_freq_khz=SDMMC_FREQ_DEFAULT;
  sd_pwr_ctrl_ldo_config_t power{}; power.ldo_chan_id=4;
  esp_err_t err=sd_pwr_ctrl_new_on_chip_ldo(&power,&host.pwr_ctrl_handle);
  sdmmc_card_t *card=nullptr;
  if (err==ESP_OK) {
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT(); slot.width=4;
    slot.clk=GPIO_NUM_43; slot.cmd=GPIO_NUM_44; slot.d0=GPIO_NUM_39; slot.d1=GPIO_NUM_40; slot.d2=GPIO_NUM_41; slot.d3=GPIO_NUM_42;
    slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t cfg{}; cfg.format_if_mount_failed=false; cfg.max_files=3; cfg.allocation_unit_size=16384;
    err=esp_vfs_fat_sdmmc_mount("/sdcard",&host,&slot,&cfg,&card);
  }
  mounted_=err==ESP_OK;
  if (mounted_) {
    sd_capacity_->publish_state(static_cast<double>(card->csd.capacity)*card->csd.sector_size/1000000.0);
    sd_status_="Mounted"; ESP_LOGI(TAG,"SD card mounted on slot 0: %s",card->cid.name);
  } else {
    if (host.pwr_ctrl_handle) sd_pwr_ctrl_del_on_chip_ldo(host.pwr_ctrl_handle);
    sd_status_="No card or unreadable filesystem";
    ESP_LOGW(TAG,"SD mount failed: %s (no card, or unsupported filesystem)",esp_err_to_name(err));
  }
  sd_mounted_->publish_state(mounted_); sd_status_sensor_->publish_state(sd_status_);
}
std::string BadgeHardware::directory() const {
  if (!mounted_) return "SD card: "+sd_status_+"\n";
  DIR *dir=opendir("/sdcard"); if (!dir) return "Cannot read SD root\n";
  std::string result="SD card /\n"; unsigned count=0;
  while (auto *entry=readdir(dir)) {
    if (++count>40) { result+="... (listing limited to 40 entries)\n"; break; }
    result+=entry->d_name; result+="\n";
  }
  closedir(dir); return result;
}
std::string BadgeHardware::summary() const {
  char gas_label[100];
  if (gas_level_=="Out of range") snprintf(gas_label,sizeof(gas_label),"GAS / OUT OF RANGE / NO TREND");
  else if (gas_baseline_>0) snprintf(gas_label,sizeof(gas_label),"GAS / %s / %s (1 min)",gas_level_.c_str(),gas_trend_.c_str());
  else if (gas_started_valid_) {
    uint32_t elapsed=(millis()-gas_started_)/1000;
    snprintf(gas_label,sizeof(gas_label),"GAS / LEARNING %u/300 s / %s",(unsigned)std::min<uint32_t>(elapsed,300),gas_trend_.c_str());
  } else snprintf(gas_label,sizeof(gas_label),"GAS / WAITING FOR VALID SAMPLE");
  char buf[600];
  snprintf(buf,sizeof(buf),"ENVIRONMENT / BME690\n\nTEMP       %7.2f C\nHUMIDITY   %7.1f %%\nPRESSURE   %7.1f hPa\n%s\nRelative to baseline / not VOC ppm\n\nMOTION / BMI270%s\n\nACCEL X    %+7.3f g\n      Y    %+7.3f g\n      Z    %+7.3f g\nGYRO  X    %+7.1f deg/s\n      Y    %+7.1f deg/s\n      Z    %+7.1f deg/s\n\nSTORAGE / %s\n",temperature_->state,humidity_->state,pressure_->state,gas_label,motion_ready_ ? "" : " / WAITING",motion_data_.acceleration[0],motion_data_.acceleration[1],motion_data_.acceleration[2],motion_data_.angular_rate[0],motion_data_.angular_rate[1],motion_data_.angular_rate[2],sd_status_.c_str());
  return buf;
}
void BadgeHardware::reset_gas_baseline() {
  gas_started_valid_=false; gas_started_=0; baseline_samples_=0; baseline_log_sum_=0; gas_baseline_=0;
  gas_history_pos_=0; gas_history_count_=0; gas_level_="Learning"; gas_trend_="Waiting";
  gas_level_sensor_->publish_state(gas_level_); gas_trend_sensor_->publish_state(gas_trend_);
  gas_response_->publish_state(NAN);
  ESP_LOGI(TAG,"Relative gas baseline reset; learning for five minutes");
}
void BadgeHardware::dump_config() {
  ESP_LOGCONFIG(TAG,"WHY2025 peripherals: BME690 and SDMMC slot 0"); LOG_I2C_DEVICE(this);
}
}
