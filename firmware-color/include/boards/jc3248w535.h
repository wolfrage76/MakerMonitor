#pragma once
// Guition JC3248W535 (3.5" 320x480 IPS, AXS15231B display + touch in one
// chip, QSPI panel with its own GRAM; ESP32-S3 N16R8, native USB). Panel pins
// and init table from Arduino_GFX's JC3248W535 device, touch protocol from
// Espressif's esp_lcd_axs15231b.
#define BOARD_NAME     "Guition 3.5"
#define BOARD_ID       "jc3248w535"
#define BOARD_OTA_ASSET "firmware-jc3248w535.bin"
#define BOARD_AP       "MakerMonitor-JC35"
#define BOARD_PPI      165   // 577 px diagonal over 3.5"

// Portrait glass driven as 480x320 landscape: LVGL, the slide compositor and
// /screenshot.png all see the landscape framebuffer, and each frame is
// rotated into the panel as it is sent.
#define LCD_QSPI_AXS15231B 1
#define LCD_ROTATION     LV_DISP_ROT_NONE
#define LCD_NATIVE_W     480
#define LCD_NATIVE_H     320
#define LCD_PANEL_W      320
#define LCD_PANEL_H      480
// 1 turns the picture (and touch with it) by 180 degrees.
#define LCD_FLIP         0

#define LCD_PIN_CS    45
#define LCD_PIN_SCK   47
#define LCD_PIN_D0    21
#define LCD_PIN_D1    48
#define LCD_PIN_D2    40
#define LCD_PIN_D3    39
#define LCD_SPI_HZ    (40 * 1000 * 1000)
#define LCD_PIN_BL    1
#define LCD_BL_ACTIVE_HIGH 1

// AXS15231B touch on I2C. Raw points are in portrait panel coordinates.
#define INPUT_TOUCH   1
#define TOUCH_AXS15231B 1
#define TOUCH_SDA     4
#define TOUCH_SCL     8
#define TOUCH_INT     3
#define TOUCH_ADDR    0x3B
#define TOUCH_SWAP_XY 1
#define TOUCH_FLIP_X  LCD_FLIP
#define TOUCH_FLIP_Y  (!LCD_FLIP)

// I2S amplifier on the speaker connector (pins from community configs for
// this board; the boot log prints I2S OK).
#define AUDIO_I2S     1
#define AUDIO_BCLK    42
#define AUDIO_LRCLK   2
#define AUDIO_DOUT    41
