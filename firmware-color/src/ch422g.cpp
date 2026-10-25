#include "ch422g.h"

#include "board.h"

#if LCD_CH422G
#include <Wire.h>

static uint8_t levels = 0;

static bool cmd(uint8_t addr, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool ch422g_begin(uint8_t initial) {
  levels = initial;
  bool out = cmd(0x38, levels);
  return cmd(0x24, 0x01) && out;  // EXIO as outputs
}

bool ch422g_write(uint8_t pin, bool high) {
  levels = high ? levels | (1u << pin) : levels & ~(1u << pin);
  return cmd(0x38, levels);
}
#endif
