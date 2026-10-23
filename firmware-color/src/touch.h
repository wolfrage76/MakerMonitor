#pragma once

// Registers the board's touchscreen as an LVGL pointer (no-op without touch).
// Call after display_init().
void touch_init();
