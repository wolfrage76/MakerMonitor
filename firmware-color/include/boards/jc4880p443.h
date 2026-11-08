#pragma once
// Guition JC4880P443C (4.3" 480x800 IPS, ST7701S over 2-lane MIPI-DSI, GT911
// touch; ESP32-P4 rev 3 with 32 MB PSRAM and 16 MB flash, Wi-Fi from an
// ESP32-C6 over SDIO). Pins, DSI timings and the C6 notes come from Wofl's
// Digital Swatch firmware, which runs on this board.
#define BOARD_NAME     "Guition 4.3"
#define BOARD_ID       "jc4880p443"
#define BOARD_OTA_ASSET "firmware-jc4880p443.bin"
#define BOARD_AP       "MakerMonitor-JC43"
#define BOARD_PPI      217   // 933 px diagonal over 4.3"

// Portrait glass driven as 800x480 landscape: LVGL, the slide compositor and
// /screenshot.png all see the landscape framebuffer. The PPA turns each frame
// onto the glass (see p4_dsi.c).
#define LCD_MIPI_P4      1
#define LCD_ROTATION     LV_DISP_ROT_NONE
#define LCD_NATIVE_W     800
#define LCD_NATIVE_H     480
#define LCD_PIN_BL       23
#define LCD_BL_ACTIVE_HIGH 1
#define LCD_BL_MIN_PCT   10

// GT911 on I2C, INT not wired (polled). Raw points are portrait 480x800:
// landscape x is raw y, landscape y is 479 - raw x.
#define INPUT_TOUCH   1
#define TOUCH_GT911   1
#define TOUCH_SDA     7
#define TOUCH_SCL     8
#define TOUCH_RST     3
#define TOUCH_SWAP_XY 1
#define TOUCH_FLIP_Y  1
