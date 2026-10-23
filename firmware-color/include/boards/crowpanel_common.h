#pragma once
// Shared by the Elecrow CrowPanel ESP32-S3 HMI 4.3" / 5.0" / 7.0" boards:
// parallel RGB panel (no init sequence), backlight PWM on GPIO 2, landscape
// natively, touch on the panel. USB-C goes through a USB-UART bridge to UART0.
#define LCD_ROTATION     LV_DISP_ROT_NONE
#define LCD_PIN_BL       2
#define LCD_BL_ACTIVE_HIGH 1
// No bounce buffer: its ISR restarts GDMA from flash-resident code, which
// panics when a flash write (NVS, LittleFS, OTA) has the cache disabled.
#define LCD_BOUNCE_LINES 0
#define INPUT_TOUCH      1
