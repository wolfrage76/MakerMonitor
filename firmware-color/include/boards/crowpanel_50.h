#pragma once
// Elecrow CrowPanel ESP32-S3 HMI 5.0" (800x480, ILI6122, GT911 touch,
// ESP32-S3-WROOM-1-N4R8). Pins from the Elecrow wiki; unverified here.
#include "crowpanel_common.h"

#define BOARD_NAME     "CrowPanel 5.0"
#define BOARD_ID       "crowpanel_50"
#define BOARD_OTA_ASSET "firmware-crowpanel-50.bin"
#define BOARD_AP       "MakerMonitor-CP50"
#define BOARD_PPI      187   // 933 px diagonal over 5.0"

// esp_lcd order D0..D15 = B0-B4, G0-G5, R0-R4.
#define LCD_DATA_PINS {8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14}
#define LCD_PIN_DE    40
#define LCD_PIN_HSYNC 39
#define LCD_PIN_VSYNC 41
#define LCD_PIN_PCLK  0
#define LCD_NATIVE_W  800
#define LCD_NATIVE_H  480
#define LCD_PCLK_HZ   (15 * 1000 * 1000)
#define LCD_HSYNC_FP  8
#define LCD_HSYNC_PW  4
#define LCD_HSYNC_BP  43
#define LCD_VSYNC_FP  8
#define LCD_VSYNC_PW  4
#define LCD_VSYNC_BP  12
#define LCD_PCLK_NEG  1

// Capacitive GT911 on I2C (also the I2C header). Board v3 holds the touch
// chip in reset through a PCA9557 expander until it is released.
#define TOUCH_GT911   1
#define TOUCH_SDA     19
#define TOUCH_SCL     20
#define TOUCH_PCA9557 0x18
