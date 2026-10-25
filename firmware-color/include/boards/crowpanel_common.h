#pragma once
// Shared by the Elecrow CrowPanel ESP32-S3 HMI 4.3" / 5.0" / 7.0" boards:
// parallel RGB panel (no init sequence), backlight PWM on GPIO 2, landscape
// natively, touch on the panel. USB-C goes through a USB-UART bridge to UART0.
#define LCD_ROTATION     LV_DISP_ROT_NONE
#define LCD_PIN_BL       2
#define LCD_BL_ACTIVE_HIGH 1
// No bounce buffer: its ISR restarts GDMA from flash-resident code, which
// panics when a flash write (NVS, LittleFS, OTA) has the cache disabled.
// (Tried on the 7" with XIP: without LCD_RGB_ISR_IRAM_SAFE the panel object
// sits in PSRAM, GDMA refuses the refill callback and the glass stays black;
// it would also cost 32 KB of internal RAM. Updates instead halve the pixel
// clock; see display_update_mode().)
#define LCD_BOUNCE_LINES 0
#define INPUT_TOUCH      1
