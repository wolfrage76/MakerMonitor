#pragma once
// Elecrow CrowPanel ESP32-S3 HMI 7.0" (800x480, EK9716, GT911 touch,
// ESP32-S3-WROOM-1-N4R8). Pins and timings match Wofl's Digital Swatch
// firmware, which runs on this board.
#include "crowpanel_common.h"

#define BOARD_NAME     "CrowPanel 7.0"
#define BOARD_ID       "crowpanel_70"
#define BOARD_OTA_ASSET "firmware-crowpanel-70.bin"
#define BOARD_AP       "MakerMonitor-CP70"
#define BOARD_PPI      133   // 933 px diagonal over 7.0"

// esp_lcd order D0..D15 = B0-B4, G0-G5, R0-R4.
#define LCD_DATA_PINS {15, 7, 6, 5, 4, 9, 46, 3, 8, 16, 1, 14, 21, 47, 48, 45}
#define LCD_PIN_DE    41
#define LCD_PIN_HSYNC 39
#define LCD_PIN_VSYNC 40
#define LCD_PIN_PCLK  0
#define LCD_PIN_PANEL_EN 38   // held LOW to enable the panel
#define LCD_NATIVE_W  800
#define LCD_NATIVE_H  480
#define LCD_PCLK_HZ   (12 * 1000 * 1000)
#define LCD_HSYNC_FP  40
#define LCD_HSYNC_PW  48
#define LCD_HSYNC_BP  40
#define LCD_VSYNC_FP  1
#define LCD_VSYNC_PW  31
#define LCD_VSYNC_BP  13
#define LCD_PCLK_NEG  1

// Capacitive GT911 on I2C (also the I2C header). Board v3 holds the touch
// chip in reset through a PCA9557 expander until it is released.
#define TOUCH_GT911   1
#define TOUCH_SDA     19
#define TOUCH_SCL     20
#define TOUCH_PCA9557 0x18

// NS4168 I2S amplifier on the SPK connector (passive 8 ohm speaker).
#define AUDIO_I2S     1
#define AUDIO_BCLK    42
#define AUDIO_LRCLK   18
#define AUDIO_DOUT    17
