#pragma once
// Elecrow CrowPanel ESP32-S3 HMI 4.3" (480x272, NV3047, resistive XPT2046,
// ESP32-S3-WROOM-1-N4R2). Pins from Elecrow's gfx_conf.h; unverified here.
#include "crowpanel_common.h"

#define BOARD_NAME     "CrowPanel 4.3"
#define BOARD_ID       "crowpanel_43"
#define BOARD_OTA_ASSET "firmware-crowpanel-43.bin"
#define BOARD_AP       "MakerMonitor-CP43"
#define BOARD_PPI      128   // 552 px diagonal over 4.3"

// esp_lcd order D0..D15 = B0-B4, G0-G5, R0-R4.
#define LCD_DATA_PINS {8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14}
#define LCD_PIN_DE    40
#define LCD_PIN_HSYNC 39
#define LCD_PIN_VSYNC 41
#define LCD_PIN_PCLK  42
#define LCD_NATIVE_W  480
#define LCD_NATIVE_H  272
#define LCD_PCLK_HZ   (8 * 1000 * 1000)
#define LCD_HSYNC_FP  8
#define LCD_HSYNC_PW  4
#define LCD_HSYNC_BP  43
#define LCD_VSYNC_FP  8
#define LCD_VSYNC_PW  4
#define LCD_VSYNC_BP  12
#define LCD_PCLK_NEG  1

// Resistive touch, SPI (shared with the SD slot, which isn't used).
#define TOUCH_XPT2046 1
#define TOUCH_SCK     12
#define TOUCH_MOSI    11
#define TOUCH_MISO    13
#define TOUCH_CS      0
#define TOUCH_INT     36
// Raw ADC range at the screen edges. Adjust if taps land off target.
#define TOUCH_RAW_X0  100
#define TOUCH_RAW_X1  4000
#define TOUCH_RAW_Y0  100
#define TOUCH_RAW_Y1  4000
