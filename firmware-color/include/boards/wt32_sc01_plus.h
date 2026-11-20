#pragma once
// Wireless-Tag WT32-SC01 Plus (3.5" 320x480 IPS, ST7796 over an 8-bit 8080
// parallel bus with its own GRAM, FT6336U touch, ESP32-S3 N16R2: 16MB flash,
// 2MB quad PSRAM, native USB). Pins from RIOT's board page and homeding's
// board notes (they agree); the bus is write-only (no RD, no CS).
#define BOARD_NAME     "WT32-SC01 Plus"
#define BOARD_ID       "wt32_sc01_plus"
#define BOARD_OTA_ASSET "firmware-wt32-sc01-plus.bin"
#define BOARD_AP       "MakerMonitor-SC01"
#define BOARD_PPI      165   // 577 px diagonal over 3.5"

// Portrait glass turned to 480x320 landscape by the panel itself (MADCTL),
// so LVGL, the slide compositor and /screenshot.png see the same pixels.
#define LCD_I80_ST7796   1
#define LCD_ROTATION     LV_DISP_ROT_NONE
#define LCD_NATIVE_W     480
#define LCD_NATIVE_H     320
// 1 turns the picture (and touch with it) by 180 degrees.
#define LCD_FLIP         0
// Not yet checked on the glass: if colours come out swapped red/blue, flip
// LCD_BGR; if they come out negative, flip LCD_INVERT.
#define LCD_BGR          1
#define LCD_INVERT       1

#define LCD_PIN_WR    47
#define LCD_PIN_DC    0
#define LCD_PIN_RST   4    // also resets the touch chip
#define LCD_DATA8_PINS {9, 46, 3, 8, 18, 17, 16, 15}
#define LCD_I80_HZ    (20 * 1000 * 1000)
#define LCD_PIN_BL    45
#define LCD_BL_ACTIVE_HIGH 1

// FT6336U touch on I2C. Raw points are in portrait panel coordinates.
#define INPUT_TOUCH   1
#define TOUCH_FT6336  1
#define TOUCH_SDA     6
#define TOUCH_SCL     5
#define TOUCH_INT     7
#define TOUCH_ADDR    0x38
#define TOUCH_SWAP_XY 1
#define TOUCH_FLIP_X  LCD_FLIP
#define TOUCH_FLIP_Y  (!LCD_FLIP)
