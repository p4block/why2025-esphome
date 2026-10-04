#include "badge_keyboard.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/json/json_util.h"
#include "esphome/core/version.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <cctype>

namespace esphome::badge_keyboard {
namespace {
std::string fold(std::string text) {
  std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){ return std::tolower(c); });
  return text;
}
}

void BadgeKeyboard::diagnostics_ui_(uint8_t key, bool pressed) {
  lv_label_set_text_fmt(body_,
    "DEVICE / ESPHome " ESPHOME_VERSION "\nESP32-P4 / 32 MB PSRAM / 16 MB flash\n"
    "Uptime: %us   Free internal RAM: %u bytes\n\n"
    "KEYBOARD / %u events\nScan: 0x%02X   %s   Character: %c\n\n"
    "Shift: %s   Ctrl: %s   Alt: %s\nFn: %s   Caps: %s\n\n"
    "All three space switches map to Space.",
    unsigned(millis()/1000),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
    unsigned(events_),key,key ? (pressed ? "DOWN" : "UP") : "Ready",
    keys_.character(key) ? keys_.character(key) : '-',
    keys_.shift() ? "on" : "off",keys_.ctrl() ? "on" : "off",keys_.alt() ? "on" : "off",
    keys_.fn() ? "on" : "off",keys_.caps ? "on" : "off");
}

bool BadgeKeyboard::lights_online_() const {
  return api::global_api_server->is_connected_with_state_subscription() && catalog_received_ &&
         millis()-lights_received_<45000;
}

void BadgeKeyboard::request_lights_() {
  if (!api::global_api_server->is_connected_with_state_subscription()) return;
  discovery_at_=millis();
  fire_homeassistant_event("esphome.whybadge_lights_discover");
}

void BadgeKeyboard::set_light_message_(const std::string &text) {
  light_message_=text; light_message_at_=millis();
}

void BadgeKeyboard::filter_lights_(const std::string &selected_id) {
  filtered_lights_.clear();
  auto query=fold(light_filter_);
  for (size_t i=0;i<lights_.size();++i) {
    auto &l=lights_[i];
    if (l.state!="on" && l.state!="off") continue;
    if (query.empty() || fold(l.name+" "+l.area+" "+l.id).find(query)!=std::string::npos)
      filtered_lights_.push_back(i);
  }
  if (filtered_lights_.empty()) { light_selected_=0; return; }
  light_selected_=std::min(light_selected_,filtered_lights_.size()-1);
  if (!selected_id.empty()) {
    for (size_t i=0;i<filtered_lights_.size();++i)
      if (lights_[filtered_lights_[i]].id==selected_id) { light_selected_=i; break; }
  }
}

void BadgeKeyboard::sync_lights(std::string payload) {
  // Bounded atomic catalog updates avoid accumulating dynamic subscriptions.
  if (payload.size()>30500) { ESP_LOGW("house_lights","Catalog too large"); return; }
  // HA normally converts JSON-looking template output back into a dict. The
  // wire prefix keeps the automation argument a string, including empty lists.
  auto first=payload.find_first_not_of(" \r\n\t");
  if (first!=std::string::npos && payload.compare(first,5,"WHY1:")==0) payload.erase(0,first+5);
  auto doc=json::parse_json(payload);
  if (!doc["lights"].is<JsonArray>() || !doc["total"].is<unsigned>()) {
    ESP_LOGW("house_lights","Invalid discovery payload; keeping previous catalog"); return;
  }
  auto array=doc["lights"].as<JsonArray>();
  if (array.size()>128) { ESP_LOGW("house_lights","Too many lights in catalog"); return; }
  std::string selected_id;
  if (light_selected_<filtered_lights_.size()) selected_id=lights_[filtered_lights_[light_selected_]].id;
  std::vector<HouseLight> next;
  next.reserve(array.size());
  for (JsonObject item : array) {
    HouseLight l;
    l.id=item["id"] | ""; l.name=item["name"] | ""; l.area=item["area"] | ""; l.state=item["state"] | "unknown";
    if (l.id.compare(0,6,"light.")!=0 || l.id.size()>160 || l.name.size()>240 || l.area.size()>160 ||
        l.state.size()>20 || std::any_of(next.begin(),next.end(),[&](const auto &v){return v.id==l.id;})) {
      ESP_LOGW("house_lights","Invalid light record; keeping previous catalog"); return;
    }
    if (l.name.empty()) l.name=l.id.substr(6);
    l.brightness=std::clamp(item["b"].as<int>(),0,100);
    l.dimmable=item["dim"] | false;
    for (auto &old : lights_) if (old.id==l.id && old.pending) {
      bool confirmed=(l.state=="on" || l.state=="off") && ((l.state=="on")==old.desired_on) &&
        (old.desired_brightness<0 || std::abs(l.brightness-old.desired_brightness)<=2);
      if (!confirmed && millis()-old.sent_at<3000) {
        l.pending=true; l.desired_on=old.desired_on;
        l.desired_brightness=old.desired_brightness; l.sent_at=old.sent_at;
      }
      break;
    }
    next.push_back(std::move(l));
  }
  std::sort(next.begin(),next.end(),[](const auto &a,const auto &b) {
    auto an=fold(a.area+" "+a.name), bn=fold(b.area+" "+b.name);
    return an==bn ? a.id<b.id : an<bn;
  });
  lights_=std::move(next);
  lights_total_=doc["total"].as<unsigned>();
  lights_received_=millis(); catalog_received_=true;
  filter_lights_(selected_id);
  ESP_LOGD("house_lights","Catalog synced: %u/%u lights",unsigned(lights_.size()),lights_total_);
  if (ready_ && active_==4) lights_ui_();
}

void BadgeKeyboard::lights_ui_() {
  for (auto *row : light_rows_) lv_obj_add_flag(row,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(light_detail_,LV_OBJ_FLAG_HIDDEN);
  if (!catalog_received_) {
    const bool connected=api::global_api_server->is_connected_with_state_subscription();
    lv_label_set_text(body_,connected ?
      "WAITING FOR LIGHT DISCOVERY\n\nEnable Home Assistant actions for this badge\nand install the supplied lights automation.\n\nLight names and states will appear here\nautomatically. No entity list to maintain." :
      "HOME ASSISTANT OFFLINE\n\nConnect this badge to your home Wi-Fi\nwith the Cloud key, then add its ESPHome\nintegration in Home Assistant.\n\nThis panel will discover your lights\nonce the supplied automation is installed.");
    lv_label_set_text(status_,"R: retry discovery | Cloud: Wi-Fi setup | Esc: desktop");
    return;
  }
  const bool online=lights_online_();
  lv_label_set_text_fmt(body_,"%s / %u lights    Filter: %s",online ? "HOME" : "OFFLINE / cached",
    unsigned(filtered_lights_.size()),light_filter_.empty() ? "type to search" : light_filter_.c_str());
  if (filtered_lights_.empty()) {
    lv_obj_remove_flag(light_detail_,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(light_detail_,light_filter_.empty() ? "No available lights in Home Assistant." : "No matching lights. Tab clears the filter.");
  } else {
    const size_t first=(light_selected_/8)*8;
    for (size_t row=0;row<8 && first+row<filtered_lights_.size();++row) {
      const size_t index=first+row;
      auto &l=lights_[filtered_lights_[index]];
      if (l.pending && millis()-l.sent_at>=3000) {
        l.pending=false; set_light_message_("No confirmation yet / showing reported state");
      }
      bool on=l.pending ? l.desired_on : l.state=="on";
      int percent=l.pending && l.desired_brightness>=0 ? l.desired_brightness : l.brightness;
      bool available=l.state=="on" || l.state=="off";
      lv_obj_remove_flag(light_rows_[row],LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_bg_color(light_rows_[row],lv_color_hex(index==light_selected_ ? 0x183641 : 0x080e14),0);
      lv_obj_set_style_text_color(light_names_[row],lv_color_hex(available ? 0xd1e6e9 : 0x567884),0);
      lv_label_set_text_fmt(light_names_[row],"%s%s",index==light_selected_ ? "> " : "  ",l.name.c_str());
      lv_obj_set_style_text_color(light_states_[row],lv_color_hex(on ? 0x63e8ef : 0x567884),0);
      if (!available) lv_label_set_text(light_states_[row],"Unavailable");
      else if (on && l.dimmable) lv_label_set_text_fmt(light_states_[row],"%d%%%s",percent,l.pending ? " ..." : "");
      else lv_label_set_text_fmt(light_states_[row],"%s%s",on ? "ON" : "OFF",l.pending ? " ..." : "");
    }
    auto &selected=lights_[filtered_lights_[light_selected_]];
    lv_obj_remove_flag(light_detail_,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(light_detail_,"%s / %u-%u of %u%s\n%s",
      selected.area.empty() ? "No area" : selected.area.c_str(),unsigned(first+1),
      unsigned(std::min(first+8,filtered_lights_.size())),unsigned(filtered_lights_.size()),
      lights_total_>lights_.size() ? " (catalog limited)" : "",selected.id.c_str());
  }
  if (!online) lv_label_set_text(status_,"Offline: controls disabled | Ctrl+R: reconnect discovery");
  else if (!light_message_.empty() && millis()-light_message_at_<2500) lv_label_set_text(status_,light_message_.c_str());
  else lv_label_set_text(status_,"Up/Down: select | Enter/Space: toggle | Left/Right: dim | Tab: reset");
}

void BadgeKeyboard::light_action_(bool toggle, int step) {
  if (!lights_online_()) { set_light_message_("Home Assistant offline / command not sent"); return; }
  if (filtered_lights_.empty()) return;
  auto &l=lights_[filtered_lights_[light_selected_]];
  if (l.state!="on" && l.state!="off") { set_light_message_("This light is unavailable"); return; }
  bool on=l.pending ? l.desired_on : l.state=="on";
  int brightness=l.pending && l.desired_brightness>=0 ? l.desired_brightness : l.brightness;
  std::map<std::string,std::string> data{{"entity_id",l.id}};
  if (toggle) {
    l.desired_on=!on; l.desired_brightness=-1;
    call_homeassistant_service(l.desired_on ? "light.turn_on" : "light.turn_off",data);
  } else {
    if (!l.dimmable) { set_light_message_("This light supports on/off only"); return; }
    if (millis()-light_dim_at_<140) return;
    light_dim_at_=millis();
    int target=std::clamp((on ? brightness : 0)+step,0,100);
    l.desired_on=target>0; l.desired_brightness=target>0 ? target : -1;
    if (target>0) data["brightness_pct"]=std::to_string(target);
    call_homeassistant_service(target>0 ? "light.turn_on" : "light.turn_off",data);
  }
  l.pending=true; l.sent_at=millis();
  set_light_message_(l.name+" / command sent");
}

void BadgeKeyboard::lights_key_(uint8_t key, char c, bool repeating) {
  if (keys_.ctrl() && (c=='r' || c=='R')) { if (!repeating) request_lights_(); }
  else if (!catalog_received_ && (c=='r' || c=='R')) { if (!repeating) request_lights_(); }
  else if (keys_.ctrl() || keys_.alt() || keys_.fn()) return;
  else if (key==0x37 && !filtered_lights_.empty()) {
    light_selected_=(light_selected_+filtered_lights_.size()-1)%filtered_lights_.size();
  } else if (key==0x34 && !filtered_lights_.empty()) {
    light_selected_=(light_selected_+1)%filtered_lights_.size();
  } else if (key==0x33 || key==0x35) light_action_(false,key==0x35 ? 10 : -10);
  else if ((key==0x3b || c==' ') && !repeating) light_action_(true,0);
  else if (key==0x15) { light_filter_.clear(); light_selected_=0; filter_lights_(); }
  else if (key==8) { if (!light_filter_.empty()) light_filter_.pop_back(); light_selected_=0; filter_lights_(); }
  else if (c && c!=' ' && light_filter_.size()<40) { light_filter_+=c; light_selected_=0; filter_lights_(); }
  lights_ui_();
}
}  // namespace esphome::badge_keyboard
