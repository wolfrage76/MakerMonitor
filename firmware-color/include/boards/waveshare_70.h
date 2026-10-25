#pragma once
// Waveshare ESP32-S3-Touch-LCD-7 (800x480 RGB, GT911 touch, CH422G I/O
// expander for the backlight and the touch / LCD resets; 16 MB flash, 8 MB
// octal PSRAM, native USB). Pins and timings from Wofl's Digital Swatch
// firmware, which runs on this board.
#define BOARD_NAME     "Waveshare 7"
#define BOARD_ID       "waveshare_70"
#define BOARD_OTA_ASSET "firmware-waveshare-70.bin"
#define BOARD_AP       "MakerMonitor-WS70"
#define BOARD_PPI      133   // 933 px diagonal over 7.0"

#define LCD_ROTATION     LV_DISP_ROT_NONE
#define LCD_BOUNCE_LINES 0
#define INPUT_TOUCH      1

// esp_lcd order D0..D15 = B3-B7, G2-G7, R3-R7.
#define LCD_DATA_PINS {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40}
#define LCD_PIN_DE    5
#define LCD_PIN_HSYNC 46
#define LCD_PIN_VSYNC 3
#define LCD_PIN_PCLK  7
#define LCD_NATIVE_W  800
#define LCD_NATIVE_H  480
#define LCD_PCLK_HZ   (14 * 1000 * 1000)
#define LCD_HSYNC_FP  8
#define LCD_HSYNC_PW  4
#define LCD_HSYNC_BP  8
#define LCD_VSYNC_FP  8
#define LCD_VSYNC_PW  4
#define LCD_VSYNC_BP  8
#define LCD_PCLK_NEG  1

// CH422G on the touch I2C bus. Its EXIO pins: backlight (on/off only, no
// PWM), touch reset, LCD reset. EXIO5 low keeps USB on GPIO19/20.
#define LCD_CH422G        1
#define CH422G_TOUCH_RST  1
#define CH422G_BACKLIGHT  2
#define CH422G_LCD_RST    3

#define TOUCH_GT911   1
#define TOUCH_SDA     8
#define TOUCH_SCL     9
#define TOUCH_INT     4
