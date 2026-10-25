// LVGL 8.3 config. Anything not set here uses LVGL's defaults.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

// LVGL objects and styles live in PSRAM. They are small, so plain malloc kept
// them in internal RAM (~150 KB on the 7"), leaving too little for Wi-Fi's
// setup access point. Draw buffers stay internal (display.cpp).
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <esp_heap_caps.h>
#define LV_MEM_CUSTOM_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE heap_caps_free
#define LV_MEM_CUSTOM_REALLOC(p, size) heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 20
#define LV_INDEV_DEF_READ_PERIOD 10  // touch sampled every 10 ms (smoother drags)
#define LV_DPI_DEF 160

#define LV_USE_LOG 0
// lv_label_set_text_fmt prints points with %.1f
#define LV_SPRINTF_USE_FLOAT 1
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_CANVAS 1
#define LV_USE_QRCODE 1

// Widgets the UI never creates are compiled out to save flash. Kept: obj,
// label, img, bar, btn, btnmatrix + textarea + keyboard (Wi-Fi page), roller (time picker), canvas +
// qrcode (About), list, spinner + arc.
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_LINE 0
#define LV_USE_ROLLER 1  // Quiet hours time picker
#define LV_USE_SLIDER 1  // Settings: brightness
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_LED 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
// Page snapshots for the slide compositor (display.cpp / ui.cpp).
#define LV_USE_SNAPSHOT 1

#endif
#endif
