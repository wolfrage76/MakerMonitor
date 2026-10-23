#pragma once
// Waveshare ESP32-S3-LCD-3.16 (ST7701, 320x820 RGB565). Pins from the Rev1.1
// schematic and a working ESPHome config; verify against Waveshare's demo if
// anything looks off.

#define BOARD_NAME     "Waveshare 3.16"
#define BOARD_ID       "ws316"
#define BOARD_OTA_ASSET "firmware-ws316.bin"
#define BOARD_AP       "MakerMonitor-Color"
#define BOARD_PPI      278   // 880 px diagonal over 3.16"

#define LCD_ST7701       1   // needs the SPI init sequence in display.cpp
// No bounce buffer: its ISR restarts GDMA from flash-resident code, which
// panics when a flash write (NVS, LittleFS, OTA) has the cache disabled.
#define LCD_BOUNCE_LINES 0

// RGB data bus, esp_lcd order D0..D15 = B0-B4, G0-G5, R0-R4.
#define LCD_DATA_PINS {21, 5, 45, 48, 47, 14, 13, 12, 11, 10, 9, 17, 46, 3, 8, 18}
#define LCD_PIN_DE    40
#define LCD_PIN_HSYNC 38
#define LCD_PIN_VSYNC 39
#define LCD_PIN_PCLK  41
#define LCD_PIN_RST   16
// 3-wire SPI used only once, to send the ST7701 init sequence.
#define LCD_PIN_CS    0
#define LCD_PIN_SCK   2
#define LCD_PIN_SDA   1
#define LCD_PIN_BL    6
#define LCD_BL_ACTIVE_HIGH 1

#define LCD_NATIVE_W  320
#define LCD_NATIVE_H  820
#define LCD_PCLK_HZ   (18 * 1000 * 1000)
#define LCD_HSYNC_BP  30
#define LCD_HSYNC_FP  30
#define LCD_HSYNC_PW  6
#define LCD_VSYNC_BP  20
#define LCD_VSYNC_FP  20
#define LCD_VSYNC_PW  40
#define LCD_PCLK_NEG  0   // try 1 if the image shimmers or colors are shifted

// Landscape. If the picture is upside down, switch to LV_DISP_ROT_270.
#define LCD_ROTATION  LV_DISP_ROT_90

// No touch: a KY-040 knob does all navigation.
#define INPUT_KNOB    1

// KY-040 knob. CLK/DT on the UART header (Serial goes over USB instead).
// SW shares the BOOT button: holding the knob while powering up = flash mode.
#define KNOB_PIN_A    43
#define KNOB_PIN_B    44
#define KNOB_PIN_SW   0
#define KNOB_STEPS_PER_DETENT 4   // try 2 if one click moves two screens
#define KNOB_REVERSE  0           // 1 if turning right goes backwards

#define BAT_ADC_PIN   4           // 200k/200k divider
