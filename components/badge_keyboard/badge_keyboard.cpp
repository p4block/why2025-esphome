#include "badge_keyboard.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"
#include "esp_heap_caps.h"
extern "C" {
#include "esp_hosted_misc.h"
}
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/components/api/api_server.h"
#include <algorithm>
#include <cstdio>
#include <cctype>
#include "esphome/components/json/json_util.h"

namespace esphome::badge_keyboard {
static const char *const TAG = "badge_keyboard";

const char *BadgeKeyboard::app_name_(uint8_t app) {
  static const char *const names[]={"Desktop", "CMD", "Sensors", "Device", "Lights", "Network", "Help"};
  return app <= 6 ? names[app] : "Desktop";
}

static void brightness_reply(uint32_t id, const uint8_t *data, size_t len, void *ctx) {
  // RPC callback runs on the radio receive thread; logging only, no LVGL access.
  if (len==3) ESP_LOGI(TAG,"%s backlight confirmed: requested %u%%, actual PWM %u%%",data[0] ? "Keyboard" : "Screen",data[1],data[2]);
}

void BadgeKeyboard::setup() {
  register_service(&BadgeKeyboard::sync_lights, "sync_lights", {"payload"});
  auto err=esp_hosted_register_custom_callback(0x77687933u,brightness_reply,nullptr);
  if (err!=ESP_OK) ESP_LOGW(TAG,"Backlight acknowledgement registration failed: %s",esp_err_to_name(err));
  // TCA8418: full 8x10 matrix, hardware debounce, FIFO polling (INT is unconnected).
  // Register addresses from TI SCPS215G; physical map from BadgeVMS tca8418.c.
  const uint8_t regs[][2]={
      {0x01,0x29}, // Key/overflow IRQ enable + overflow shift mode (TI overflow erratum).
      {0x03,0x00}, // Unlock keypad.
      {0x23,0x00},{0x24,0x00},{0x25,0x00}, // GPIO input direction.
      {0x1d,0xff},{0x1e,0xff},{0x1f,0x03}, // Eight rows, ten columns.
      {0x20,0x00},{0x21,0x00},{0x22,0x00}, // No non-matrix GPIO events.
      {0x1a,0x00},{0x1b,0x00},{0x1c,0x00},
      {0x29,0x00},{0x2a,0x00},{0x2b,0x00}, // Debounce enabled.
      {0x2c,0x00},{0x2d,0x00},{0x2e,0x00}, // Pull-ups enabled.
  };
  for (auto &reg : regs) {
    if (!this->write_byte(reg[0], reg[1])) {
      ESP_LOGE(TAG,"TCA8418 configuration failed at register 0x%02X",reg[0]);
      this->mark_failed(); return;
    }
  }
  for (int i=0;i<10;++i) {
    uint8_t count=0, event=0;
    if (!this->read_byte(0x03,&count) || !(count&15)) break;
    if (!this->read_byte(0x04,&event)) break;
  }
  this->write_byte(0x02,0x1f);
  ESP_LOGI(TAG,"TCA8418 keyboard initialized at 0x34; 8x10 matrix, polling 10ms");
}

void BadgeKeyboard::dump_config() {
  ESP_LOGCONFIG(TAG,"WHY2025 keyboard and desktop apps");
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
}

static lv_obj_t *label(lv_obj_t *parent,int x,int y,int w,const char *text) {
  auto *obj=lv_label_create(parent);
  lv_obj_set_pos(obj,x,y); lv_obj_set_width(obj,w);
  lv_label_set_long_mode(obj,LV_LABEL_LONG_WRAP);
  lv_label_set_text(obj,text);
  return obj;
}

static lv_obj_t *text_area(lv_obj_t *parent,int x,int y,int w,int h,bool single) {
  auto *obj=lv_textarea_create(parent);
  lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,w,h);
  lv_textarea_set_one_line(obj,single);
  lv_obj_set_style_bg_color(obj,lv_color_hex(0x080e14),0);
  lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
  lv_obj_set_style_text_color(obj,lv_color_hex(0xd1e6e9),0);
  lv_obj_set_style_border_color(obj,lv_color_hex(0x243f4b),0);
  lv_obj_set_style_radius(obj,0,0);
  lv_obj_set_style_pad_all(obj,10,0);
  lv_obj_set_style_text_font(obj,LV_FONT_DEFAULT,0);
  lv_textarea_set_text(obj,"");
  return obj;
}

void BadgeKeyboard::start_ui() {
  auto *screen=lv_screen_active();
  window_=lv_obj_create(screen);
  lv_obj_set_pos(window_,20,84); lv_obj_set_size(window_,680,534);
  lv_obj_remove_flag(window_,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(window_,0,0);
  lv_obj_set_style_pad_all(window_,0,0);
  lv_obj_set_style_bg_color(window_,lv_color_hex(0x080e14),0);
  lv_obj_set_style_border_color(window_,lv_color_hex(0x355562),0);
  lv_obj_set_style_border_width(window_,1,0);
  lv_obj_set_style_text_font(window_,LV_FONT_DEFAULT,0);
  lv_obj_set_style_text_color(window_,lv_color_hex(0xd1e6e9),0);
  title_=label(window_,14,12,650,"CONSOLE / WHY2025");
  lv_obj_set_style_text_color(title_,lv_color_hex(0x63e8ef),0);
  output_=text_area(window_,12,44,652,376,false);
  lv_obj_remove_flag(output_,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(output_,LV_OPA_TRANSP,LV_PART_CURSOR);
  input_=text_area(window_,12,430,652,48,true);
  lv_textarea_set_max_length(input_,120);
  body_=label(window_,18,54,640,"");
  status_=label(window_,14,498,650,"Enter runs a command | Up/Down history | Esc desktop");
  ssid_input_=text_area(window_,12,180,652,48,true);
  password_input_=text_area(window_,12,282,652,48,true);
  lv_textarea_set_password_mode(password_input_,true);
  lv_textarea_set_max_length(ssid_input_,32);
  lv_textarea_set_max_length(password_input_,64);
  network_status_=label(window_,18,354,630,"");
  wifi_ssid_.text=wifi::global_wifi_component->get_sta().get_ssid();
  wifi_ssid_.end();
  transcript_="WHY / CONSOLE\nType help and press Enter.\n";
  if (brightness_number_ && brightness_number_->has_state()) set_brightness(brightness_number_->state);
  if (keyboard_brightness_number_ && keyboard_brightness_number_->has_state()) set_keyboard_brightness(keyboard_brightness_number_->state);
  for (int i=0;i<6;++i) {
    dock_[i]=lv_obj_create(screen);
    lv_obj_set_pos(dock_[i],14+i*116,640); lv_obj_set_size(dock_[i],112,68);
    lv_obj_set_style_radius(dock_[i],0,0); lv_obj_set_style_bg_opa(dock_[i],LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(dock_[i],1,0);
    lv_obj_set_style_border_color(dock_[i],lv_color_hex(0x243f4b),0);
    lv_obj_remove_flag(dock_[i],LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dock_[i],LV_OBJ_FLAG_CLICKABLE);
  }
  for (int i=0;i<8;++i) {
    light_rows_[i]=lv_obj_create(window_);
    lv_obj_set_pos(light_rows_[i],12,94+i*42); lv_obj_set_size(light_rows_[i],652,40);
    lv_obj_set_style_pad_all(light_rows_[i],0,0);
    lv_obj_set_style_radius(light_rows_[i],0,0);
    lv_obj_set_style_border_width(light_rows_[i],0,0);
    lv_obj_remove_flag(light_rows_[i],LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(light_rows_[i],lv_color_hex(0x080e14),0);
    light_names_[i]=label(light_rows_[i],10,10,475,"");
    light_states_[i]=label(light_rows_[i],493,10,150,"");
    lv_label_set_long_mode(light_names_[i],LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(light_states_[i],LV_TEXT_ALIGN_RIGHT,0);
  }
  light_detail_=label(window_,18,442,640,"");
  ready_=true;
  open_app_(0);
  ESP_LOGI(TAG,"Desktop apps ready; Square opens CMD, Esc returns home");
}

void BadgeKeyboard::open_app_(uint8_t app) {
  if (app>6 || !ready_) return;
  active_=app; repeat_key_=0;
  for (auto *row : light_rows_) lv_obj_add_flag(row,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(light_detail_,LV_OBJ_FLAG_HIDDEN);
  for (int i=0;i<6;++i) lv_obj_set_style_border_color(dock_[i],lv_color_hex(app==i+1 ? 0x63e8ef : 0x243f4b),0);
  if (!app) { lv_obj_add_flag(window_,LV_OBJ_FLAG_HIDDEN); return; }
  lv_obj_remove_flag(window_,LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text_fmt(title_,"%s / WHY2025",app_name_(app));
  for (auto *obj : {output_,input_,body_,ssid_input_,password_input_,network_status_}) lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
  switch (app) {
    case 1:
      lv_obj_remove_flag(output_,LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(input_,LV_OBJ_FLAG_HIDDEN);
      lv_textarea_set_text(output_,transcript_.c_str());
      lv_textarea_set_cursor_pos(output_,LV_TEXTAREA_CURSOR_LAST);
      lv_obj_add_state(input_,LV_STATE_FOCUSED); sync_line_();
      lv_label_set_text(status_,"Enter: run | arrows: edit/history | Ctrl+C: cancel | Ctrl+L: clear"); break;
    case 2:
      lv_obj_remove_flag(body_,LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(body_,hardware_->summary().c_str());
      lv_label_set_text(status_,"Live telemetry | Esc: desktop"); break;
    default:
      lv_obj_remove_flag(body_,LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(status_,"Shape keys switch apps | Alt+Tab cycles apps | Esc returns to desktop");
      if (app==3) diagnostics_ui_();
      if (app==4) { lights_ui_(); request_lights_(); }
      if (app==5) {
        lv_label_set_text(body_,"WI-FI SETUP\n\n\n\n\nNetwork name (SSID):\n\n\n\n\n\n\nPassword (blank for open networks):");
        for (auto *obj : {ssid_input_,password_input_,network_status_}) lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);
        network_ui_();
        lv_label_set_text(status_,"Tab: switch field | Enter: save/connect | Esc: desktop");
      }
      if (app==6) lv_label_set_text(body_,"MODULE KEYS\nSquare: Console   Triangle: Sensors\nCross: Device     Circle: Lights\nCloud: Wi-Fi     Diamond: Guide\n\nEsc: desktop | Alt+Tab: cycle\nFn+1..6: module shortcuts\nFn+Up/Down: display brightness\nFn+B: audio test\n\nLIGHTS / CIRCLE\nUp/Down: select | Enter/Space: toggle\nLeft/Right: brightness +/-10%\nType: filter | Backspace: edit\nTab: clear filter | Ctrl+R: refresh\n\nCONSOLE / SQUARE\nEnter: run | Up/Down: history\nTab: complete | Ctrl+L: clear\nCtrl+C: cancel | Fn+Left/Right: Home/End");
  }
  ESP_LOGI(TAG,"App: %s",app_name_(app));
}

void BadgeKeyboard::sync_line_() {
  lv_textarea_set_text(input_,line_.text.c_str());
  lv_textarea_set_cursor_pos(input_,line_.cursor);
}

void BadgeKeyboard::print_(const std::string &text) {
  transcript_+=text;
  if (transcript_.size()>6000) {
    size_t p=transcript_.find('\n',transcript_.size()-5000);
    transcript_.erase(0,p==std::string::npos ? transcript_.size()-5000 : p+1);
  }
  lv_textarea_set_text(output_,transcript_.c_str());
  lv_textarea_set_cursor_pos(output_,LV_TEXTAREA_CURSOR_LAST);
}

void BadgeKeyboard::execute_(const std::string &command) {
  print_("WHY> "+command+"\n");
  auto start=command.find_first_not_of(' ');
  if (start==std::string::npos) return;
  auto end=command.find(' ',start);
  std::string verb=command.substr(start,end==std::string::npos ? end : end-start);
  std::transform(verb.begin(),verb.end(),verb.begin(),[](unsigned char c){return std::tolower(c);});
  std::string args=end==std::string::npos ? "" : command.substr(end+1);
  if (verb=="help") print_("help      List commands\nsensors   Show live telemetry\ngasreset  Learn new gas baseline\ndir       List SD card root\nsd        Retry SD card mount\nbeep      Play test tone\necho TEXT Print text\ncls       Clear screen\nuptime    Show running time\ninfo      Show hardware and free RAM\napps      List app shortcuts\nopen APP  cmd/sensors/device/lights/network/help/desktop\nexit      Return to desktop\n\nLocal commands run on this badge.\n");
  else if (verb=="sensors") print_(hardware_->summary());
  else if (verb=="gasreset") { hardware_->reset_gas_baseline(); print_("Learning startup gas baseline for five minutes.\n"); }
  else if (verb=="dir") print_(hardware_->directory());
  else if (verb=="sd") { hardware_->mount_sd(); print_(hardware_->directory()); }
  else if (verb=="beep") { audio_->play("badge:d=8,o=5,b=140:c,e,g,c6"); print_("Test tone on LS1 speaker connector\n"); }
  else if (verb=="echo") print_(args+"\n");
  else if (verb=="cls" || verb=="clear") { transcript_.clear(); print_(""); }
  else if (verb=="uptime") print_(std::to_string(millis()/1000)+" seconds\n");
  else if (verb=="info") print_("ESPHome " ESPHOME_VERSION " / ESP32-P4 rev1.0\nDisplay: 720x720 ST7703 / RGB565\nPSRAM: 32 MB / Flash: 16 MB\nFree internal RAM: "+std::to_string(heap_caps_get_free_size(MALLOC_CAP_INTERNAL))+" bytes\nKeyboard events: "+std::to_string(events_)+"\n");
  else if (verb=="apps") print_("Square CMD | Triangle Sensors | Cross Device\nCircle Lights | Cloud Network | Diamond Help\nEsc Desktop\n");
  else if (verb=="exit") open_app_(0);
  else if (verb=="open") {
    std::transform(args.begin(),args.end(),args.begin(),[](unsigned char c){return std::tolower(c);});
    static const char *const names[]={"desktop","cmd","sensors","device","lights","network","help"};
    if (args=="keys" || args=="system") args="device";
    bool found=false;
    for (uint8_t i=0;i<7;++i) if (args==names[i]) { open_app_(i); found=true; break; }
    if (!found) print_("Unknown app. Type apps for shortcuts.\n");
  } else print_("Unknown command: "+verb+". Type help.\n");
}

void BadgeKeyboard::key_(uint8_t key,bool repeating) {
  if (!ready_) return;
  if (keys_.fn() && (key==0x37 || key==0x34)) {
    float value=std::clamp(static_cast<float>(brightness_)+(key==0x37 ? 10 : -10),5.0f,100.0f);
    if (brightness_number_) {
      auto call=brightness_number_->make_call(); call.set_value(value); call.perform();
    } else set_brightness(value);
    lv_label_set_text_fmt(status_,"Screen brightness: %u%% | Fn+Up/Down: adjust",brightness_);
    return;
  }
  if (key==1 || key==0x3e) { open_app_(0); return; }
  if (!repeating && key>=2 && key<=7) { open_app_(key-1); return; }
  if (!repeating && keys_.fn() && key>=0x0c && key<=0x11) { open_app_(key-0x0b); return; }
  if (key==0x15 && keys_.alt()) { open_app_(active_==6 ? 0 : active_+1); return; }
  char c=keys_.character(key);
  if (!repeating && keys_.fn() && (c=='b' || c=='B')) {
    audio_->play("badge:d=8,o=5,b=140:c,e,g,c6"); return;
  }
  if (active_==1) {
    if (keys_.ctrl()) {
      if (c=='c' || c=='C') { print_("WHY> "+line_.text+"^C\n"); line_.clear(); }
      else if (c=='l' || c=='L') { transcript_.clear(); print_(""); }
      else if (c=='u' || c=='U') line_.clear();
      else if (c=='a' || c=='A') line_.home();
      else if (c=='e' || c=='E') line_.end();
    } else if (key==0x3b) execute_(line_.submit());
    else if (key==8) { if (keys_.fn()) line_.del(); else line_.backspace(); }
    else if (key==0x33) { if (keys_.fn()) line_.home(); else line_.left(); }
    else if (key==0x35) { if (keys_.fn()) line_.end(); else line_.right(); }
    else if (key==0x37) line_.previous();
    else if (key==0x34) line_.next();
    else if (key==0x15) {
      static const char *const commands[]={"help","sensors","gasreset","dir","sd","beep","echo","cls","uptime","info","apps","open","exit"};
      const char *match=nullptr; int count=0;
      for (auto *cmd : commands) if (std::string(cmd).compare(0,line_.text.size(),line_.text)==0) { match=cmd; ++count; }
      if (count==1) { line_.text=match; line_.end(); line_.insert(' '); }
    } else if (c && !keys_.alt() && !keys_.fn()) line_.insert(c);
    sync_line_();
  } else if (active_==4) {
    lights_key_(key,c,repeating);
  } else if (active_==5) {
    network_key_(key,c);
  }
}

void BadgeKeyboard::network_ui_() {
  lv_textarea_set_text(ssid_input_,wifi_ssid_.text.c_str());
  lv_textarea_set_cursor_pos(ssid_input_,wifi_ssid_.cursor);
  lv_textarea_set_text(password_input_,wifi_password_.text.c_str());
  lv_textarea_set_cursor_pos(password_input_,wifi_password_.cursor);
  lv_obj_clear_state(ssid_input_,LV_STATE_FOCUSED);
  lv_obj_clear_state(password_input_,LV_STATE_FOCUSED);
  lv_obj_add_state(wifi_field_ ? password_input_ : ssid_input_,LV_STATE_FOCUSED);
  if (!wifi::global_wifi_component->is_connected())
    lv_label_set_text(network_status_,"Enter network name and password above.\nPress Enter to save and connect.\nSaved settings reconnect after reboot.\nUse a 2.4 GHz network.");
}

void BadgeKeyboard::network_key_(uint8_t key, char c) {
  auto &field=wifi_field_ ? wifi_password_ : wifi_ssid_;
  if (key==0x15 || key==0x37 || key==0x34) wifi_field_^=1;
  else if (key==0x3b) {
    const auto &ssid=wifi_ssid_.text;
    const auto &pass=wifi_password_.text;
    if (ssid.empty() || ssid.size()>32 || (!pass.empty() && (pass.size()<8 || pass.size()>64))) {
      lv_label_set_text(network_status_,"SSID: 1-32 characters.\nPassword: 8-64 characters, or blank for open Wi-Fi.");
      return;
    }
    auto *w=wifi::global_wifi_component;
    w->disable();
    w->save_wifi_sta(ssid,pass);
    // Start without a configured STA so ESPHome uses its stable provisioning
    // preference key, then reloads the credentials just saved above.
    w->clear_sta();
    w->enable();
    wifi_password_.clear();
    network_ui_();
    lv_label_set_text(network_status_,"Settings saved. Connecting...\nIf connection fails, re-enter the password\nand check the network name.");
    return;
  } else if (keys_.ctrl() && (c=='u' || c=='U')) field.clear();
  else if (key==8) { if (keys_.fn()) field.del(); else field.backspace(); }
  else if (key==0x33) { if (keys_.fn()) field.home(); else field.left(); }
  else if (key==0x35) { if (keys_.fn()) field.end(); else field.right(); }
  else if (c && !keys_.ctrl() && !keys_.alt() && !keys_.fn() && field.text.size()<(wifi_field_ ? 64 : 32)) field.insert(c);
  network_ui_();
}

void BadgeKeyboard::set_brightness(float value) {
  brightness_=static_cast<uint8_t>(std::clamp(value,5.0f,100.0f));
  brightness_dirty_=true; brightness_retry_=millis();
  if (brightness_number_) brightness_number_->publish_state(brightness_);
}

void BadgeKeyboard::set_keyboard_brightness(float value) {
  keyboard_brightness_=static_cast<uint8_t>(std::clamp(value,0.0f,100.0f));
  keyboard_brightness_dirty_=true; brightness_retry_=millis();
  if (keyboard_brightness_number_) keyboard_brightness_number_->publish_state(keyboard_brightness_);
}

void BadgeKeyboard::fault_() {
  keys_.release_all(); repeat_key_=0;
  if (++errors_==1) ESP_LOGW(TAG,"Keyboard I2C read failed; held keys cleared");
  this->status_set_warning();
}

void BadgeKeyboard::update() {
  if (ready_ && active_==2 && millis()-sensor_refresh_>=500) {
    sensor_refresh_=millis(); lv_label_set_text(body_,hardware_->summary().c_str());
  }
  if (!ready_ || this->is_failed()) return;
  bool ha_connected=api::global_api_server->is_connected_with_state_subscription();
  if (ha_connected && (!ha_was_connected_ || millis()-discovery_at_>30000)) request_lights_();
  ha_was_connected_=ha_connected;
  if (active_==4 && millis()-lights_refresh_>=250) { lights_refresh_=millis(); lights_ui_(); }
  if (active_==5 && millis()-network_refresh_>1000) {
    network_refresh_=millis();
    auto *w=wifi::global_wifi_component;
    if (w->is_connected()) {
      char ip[network::IP_ADDRESS_BUFFER_SIZE];
      w->get_ip_addresses()[0].str_to(ip);
      lv_label_set_text_fmt(network_status_,"Connected: %s\nIP: %s\nHome Assistant: %s\n\nAdd ESPHome device: whybadge.local\nAPI port: 6053",w->get_sta().get_ssid().c_str(),ip,api::global_api_server->is_connected_with_state_subscription() ? "connected" : "waiting for pairing");
    }
  }
  if ((brightness_dirty_ || keyboard_brightness_dirty_) && static_cast<int32_t>(millis()-brightness_retry_)>=0) {
    brightness_retry_=millis()+5000;
    if (brightness_dirty_) {
      auto err=esp_hosted_send_custom_data(0x77687931u,&brightness_,1);
      if (err==ESP_OK) { brightness_dirty_=false; ESP_LOGI(TAG,"Screen brightness: %u%%",brightness_); }
      else ESP_LOGW(TAG,"Backlight request failed: %s",esp_err_to_name(err));
    }
    if (keyboard_brightness_dirty_) {
      auto err=esp_hosted_send_custom_data(0x77687932u,&keyboard_brightness_,1);
      if (err==ESP_OK) { keyboard_brightness_dirty_=false; ESP_LOGI(TAG,"Keyboard brightness: %u%%",keyboard_brightness_); }
    }
  }
  uint8_t status=0,count=0;
  if (!this->read_byte(0x02,&status) || !this->read_byte(0x03,&count)) { fault_(); return; }
  if (status&0x08) {
    keys_.release_all(); repeat_key_=0;
    // Drop the damaged FIFO so a lost modifier release cannot leave Shift/Ctrl stuck.
    for (int i=0;i<10 && (count&15);++i) {
      uint8_t raw;
      if (!this->read_byte(0x04,&raw) || !this->read_byte(0x03,&count)) { fault_(); return; }
    }
    this->write_byte(0x02,0x1f);
    ESP_LOGW(TAG,"Keyboard FIFO overflow; input reset, release keys and retry");
    lv_label_set_text(status_,"Keyboard overflow: release keys and retry.");
    return;
  }
  errors_=0; this->status_clear_warning();
  for (int i=0;i<32 && (count&15);++i) {
    uint8_t raw=0;
    if (!this->read_byte(0x04,&raw)) { fault_(); return; }
    uint8_t key=raw&0x7f;
    if (key>0 && key<=80) {
      ++events_;
      bool pressed=(raw&0x80)!=0, fresh=keys_.event(raw);
      ESP_LOGD(TAG,"Key 0x%02X %s",key,pressed ? "down" : "up");
      if (active_==3) diagnostics_ui_(key,pressed);
      if (fresh) {
        key_(key);
        if (!keys_.ctrl() && !keys_.alt() && ((keys_.character(key) && keys_.character(key)!=' ') || key==8 || key==0x33 || key==0x35 || (active_==4 && (key==0x37 || key==0x34)))) {
          repeat_key_=key; repeat_at_=millis()+450;
        }
      }
      if (!pressed && key==repeat_key_) repeat_key_=0;
    }
    if (!this->read_byte(0x03,&count)) { fault_(); return; }
  }
  if (!(count&15) && status) this->write_byte(0x02,status&0x1f);
  if (repeat_key_ && keys_.held[repeat_key_] && static_cast<int32_t>(millis()-repeat_at_)>=0) {
    key_(repeat_key_,true); repeat_at_=millis()+65;
  }
}

}  // namespace esphome::badge_keyboard
