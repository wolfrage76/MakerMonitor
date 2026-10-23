#pragma once
#include <stdint.h>

struct KnobInput {
  int steps = 0;        // + clockwise, - counter-clockwise (detents)
  bool click = false;   // released before the long-press time
  bool longPress = false;
  bool any() const { return steps || click || longPress; }
};

void knob_init();
KnobInput knob_poll();
