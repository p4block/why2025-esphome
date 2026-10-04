#include "../components/badge_keyboard/input_core.h"
#include <cassert>
#include <iostream>
using namespace esphome::badge_keyboard;
int main() {
  Keys k;
  assert(!k.event(0x80)); assert(!k.event(0xff));
  assert(k.event(0xa0)); assert(k.character(0x20)=='a');
  assert(!k.event(0xa0)); assert(!k.event(0x20));
  k.event(0xa9); assert(k.character(0x20)=='A'); assert(k.character(0x0c)=='!');
  k.event(0x38); assert(k.shift()); // Releasing other Shift doesn't lose left Shift.
  k.event(0x29); assert(!k.shift());
  k.event(0x9f); k.event(0xa9); assert(k.caps); k.event(0x29); k.event(0x1f);
  assert(k.character(0x20)=='A'); k.event(0xb8); assert(k.character(0x20)=='a');
  assert(k.character(0x50)=='}'); k.release_all(); assert(!k.shift() && !k.ctrl());
  assert(k.character(0x41)==' ' && k.character(0x42)==' ' && k.character(0x43)==' ');
  CommandLine l;
  for (char c : std::string("echo hi")) l.insert(c);
  assert(l.submit()=="echo hi"); assert(l.text.empty());
  l.insert('x'); l.previous(); assert(l.text=="echo hi"); l.next(); assert(l.text=="x");
  l.clear(); l.insert('a'); l.insert('c'); l.left(); l.insert('b'); assert(l.text=="abc");
  l.backspace(); assert(l.text=="ac"); l.del(); assert(l.text=="a");
  l.home(); l.backspace(); assert(l.text=="a");
  l.clear(); for (int i=0;i<200;++i) l.insert('x'); assert(l.text.size()==120);
  for (int i=0;i<30;++i) { l.clear(); for (char c : std::to_string(i)) l.insert(c); l.submit(); }
  assert(l.history.size()==16); l.previous(); assert(l.text=="29");
  std::cout << "Keyboard modifiers, three space keys, editor, history and bounds passed\n";
}
