#pragma once
// Waveshare ESP32-S3-Touch-LCD-2 (2" 240x320 IPS, ST7789T3 over 4-wire SPI
// with its own GRAM, CST816D touch, ESP32-S3 N16R8, native USB). Pins from
// Waveshare's wiki demos as used by Clawdmeter and MicroPythonOS.
#define BOARD_NAME     "Waveshare 2"
#define BOARD_ID       "ws_lcd2"
#define BOARD_OTA_ASSET "firmware-ws-lcd2.bin"
#define BOARD_AP       "MakerMonitor-WS2"
#define BOARD_PPI      200   // 400 px diagonal over 2"

// Portrait glass turned to 320x240 landscape by the panel itself (MADCTL),
// so LVGL, the slide compositor and /screenshot.png see the same pixels.
#define LCD_SPI_ST7789   1
#define LCD_ROTATION     LV_DISP_ROT_NONE
#define LCD_NATIVE_W     320
#define LCD_NATIVE_H     240
// 1 turns the picture (and touch with it) by 180 degrees.
#define LCD_FLIP         0

#define LCD_PIN_MOSI  38
#define LCD_PIN_SCK   39
#define LCD_PIN_CS    45
#define LCD_PIN_DC    42
#define LCD_PIN_RST   0    // shared with BOOT: pulsed once at start-up
#define LCD_SPI_HZ    (80 * 1000 * 1000)
#define LCD_PIN_BL    1
#define LCD_BL_ACTIVE_HIGH 1

// CST816D touch on I2C (shared with the QMI8658 IMU). Raw points are in
// portrait panel coordinates.
#define INPUT_TOUCH   1
#define TOUCH_CST816  1
#define TOUCH_SDA     48
#define TOUCH_SCL     47
#define TOUCH_INT     46
#define TOUCH_ADDR    0x15
#define TOUCH_SWAP_XY 1
#define TOUCH_FLIP_X  LCD_FLIP
#define TOUCH_FLIP_Y  (!LCD_FLIP)

#define BAT_ADC_PIN   5
#define BAT_DIVIDER   3    // 200k/100k: the pin sees Vbat / 3
