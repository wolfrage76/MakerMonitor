#pragma once
#include <lvgl.h>

// Sets up the panel and registers a landscape LVGL display (size per board).
void display_init();
void display_set_brightness(uint8_t pct);
