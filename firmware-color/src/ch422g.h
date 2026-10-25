#pragma once
#include <stdint.h>

// CH422G I/O expander (Waveshare 7"): EXIO0-7 as push-pull outputs. It has
// no register map; each I2C "address" is a command (0x24 mode, 0x38 outputs).
bool ch422g_begin(uint8_t levels);   // Wire must already be started
bool ch422g_write(uint8_t pin, bool high);
