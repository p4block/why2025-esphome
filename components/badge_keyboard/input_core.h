#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::badge_keyboard {

// Physical event numbers verified against BadgeVMS drivers/tca8418.c.
struct Keys {
  std::array<bool, 81> held{};
  bool caps{false};
  bool shift() const { return held[0x29] || held[0x38]; }
  bool ctrl() const { return held[0x3d]; }
  bool alt() const { return held[0x3f] || held[0x44]; }
  bool fn() const { return held[0x1f]; }
  bool event(uint8_t raw) {
    const uint8_t key = raw & 0x7f;
    if (key == 0 || key > 80) return false;
    const bool down = (raw & 0x80) != 0;
    const bool fresh = down && !held[key];
    held[key] = down;
    if (fresh && fn() && (key == 0x29 || key == 0x38)) caps = !caps;
    return fresh;
  }
  void release_all() { held.fill(false); }
  char character(uint8_t key) const {
    char c = 0, s = 0;
    if (key >= 0x0c && key <= 0x14) {
      c = "123456789"[key - 0x0c]; s = "!@#$%^&*("[key - 0x0c];
    } else if (key >= 0x16 && key <= 0x1e) c = "qwertyuio"[key - 0x16];
    else if (key >= 0x20 && key <= 0x28) c = "asdfghjkl"[key - 0x20];
    else if (key >= 0x2a && key <= 0x30) c = "zxcvbnm"[key - 0x2a];
    else switch (key) {
      case 0x09: c='0'; s=')'; break;
      case 0x0a: c='-'; s='_'; break;
      case 0x0b: c='`'; s='~'; break;
      case 0x31: c=','; s='<'; break;
      case 0x32: c='.'; s='>'; break;
      case 0x36: c='/'; s='?'; break;
      case 0x39: c=';'; s=':'; break;
      case 0x3a: c='\''; s='"'; break;
      case 0x3c: c='='; s='+'; break;
      case 0x40: c='\\'; s='|'; break;
      case 0x41: case 0x42: case 0x43: c=' '; break;
      case 0x45: c='p'; break;
      case 0x46: c='['; s='{'; break;
      case 0x50: c=']'; s='}'; break;
      default: break;
    }
    if (c >= 'a' && c <= 'z') return shift() != caps ? c - 'a' + 'A' : c;
    return shift() && s ? s : c;
  }
};

// Bounded command-line model, independent of LVGL and I2C for host tests.
class CommandLine {
 public:
  std::string text;
  size_t cursor{0};
  std::vector<std::string> history;
  size_t history_pos{0};
  std::string draft;
  void insert(char c) {
    if (c >= 32 && c <= 126 && text.size() < 120) text.insert(cursor++, 1, c);
  }
  void backspace() { if (cursor) text.erase(--cursor, 1); }
  void del() { if (cursor < text.size()) text.erase(cursor, 1); }
  void left() { if (cursor) --cursor; }
  void right() { if (cursor < text.size()) ++cursor; }
  void home() { cursor=0; }
  void end() { cursor=text.size(); }
  void clear() { text.clear(); cursor=0; history_pos=history.size(); draft.clear(); }
  std::string submit() {
    auto command=text;
    if (!command.empty() && (history.empty() || history.back() != command)) {
      if (history.size() == 16) history.erase(history.begin());
      history.push_back(command);
    }
    clear();
    return command;
  }
  void previous() {
    if (!history_pos) return;
    if (history_pos == history.size()) draft=text;
    text=history[--history_pos]; end();
  }
  void next() {
    if (history_pos >= history.size()) return;
    ++history_pos;
    text=history_pos == history.size() ? draft : history[history_pos]; end();
  }
};

}  // namespace esphome::badge_keyboard
