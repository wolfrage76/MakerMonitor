#pragma once
// Picks the board header from the PlatformIO env (-DBOARD_...). Each header
// sets the panel pins/timings and how the board is controlled:
//   INPUT_TOUCH  touchscreen: swipe left/right = page, up/down = more on the
//                page, tap = the thing tapped, menu via the hamburger
//   INPUT_KNOB   KY-040 rotary knob with push button (boards without touch)
#include <lvgl.h>

#if defined(BOARD_CROWPANEL_43)
#include "boards/crowpanel_43.h"
#elif defined(BOARD_CROWPANEL_50)
#include "boards/crowpanel_50.h"
#elif defined(BOARD_CROWPANEL_70)
#include "boards/crowpanel_70.h"
#elif defined(BOARD_WAVESHARE_70)
#include "boards/waveshare_70.h"
#elif defined(BOARD_JC3248W535)
#include "boards/jc3248w535.h"
#elif defined(BOARD_WS_LCD2)
#include "boards/ws_lcd2.h"
#else
#include "boards/ws316.h"
#endif

#ifndef PROJECT_GITHUB_REPO
#define PROJECT_GITHUB_REPO "wolfrage76/MakerMonitor"
#endif
#ifndef PROJECT_GITHUB_URL
#define PROJECT_GITHUB_URL "https://github.com/" PROJECT_GITHUB_REPO
#endif
#ifndef PROJECT_MAKERWORLD_URL
#define PROJECT_MAKERWORLD_URL "https://makerworld.com/en/@Wofl"
#endif

#ifndef BOARD_PPI
#define BOARD_PPI 160
#endif
#ifndef INPUT_TOUCH
#define INPUT_TOUCH 0
#endif
#ifndef INPUT_KNOB
#define INPUT_KNOB 0
#endif
#ifndef AUDIO_I2S
#define AUDIO_I2S 0
#endif
#ifndef LCD_ST7701
#define LCD_ST7701 0
#endif
#ifndef LCD_QSPI_AXS15231B
#define LCD_QSPI_AXS15231B 0
#endif
#ifndef LCD_SPI_ST7789
#define LCD_SPI_ST7789 0
#endif
// Panels that keep their own picture: frames go from a PSRAM framebuffer
// over SPI, and the screen survives flash writes.
#define LCD_HAS_GRAM (LCD_QSPI_AXS15231B || LCD_SPI_ST7789)
#ifndef LCD_BL_ONOFF
#define LCD_BL_ONOFF 0
#endif
#ifndef LCD_BL_MIN_PCT
#define LCD_BL_MIN_PCT 0  // PWM backlights: lowest duty that still lights (see ws316.h)
#endif
#ifndef LCD_CH422G
#define LCD_CH422G 0
#endif
#ifndef BAT_ADC_PIN
#define BAT_ADC_PIN -1
#endif
#ifndef BAT_DIVIDER
#define BAT_DIVIDER 2
#endif
#ifndef TOUCH_FLIP_X
#define TOUCH_FLIP_X 0
#endif
#ifndef TOUCH_FLIP_Y
#define TOUCH_FLIP_Y 0
#endif
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY 0
#endif
