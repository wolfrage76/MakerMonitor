#pragma once
#include <lvgl.h>

// Sets up the panel and registers a landscape LVGL display (size per board).
void display_init();
void display_set_brightness(uint8_t pct);
// Boards whose backlight is only on/off (LCD_CH422G) dim in software: pixels
// are darkened as they are written. Page snapshots for slides go through the
// same table. No-op elsewhere.
void display_dim_pixels(uint16_t* px, size_t n);
// Stops the RGB scan and frees the panel. Flash writes (OTA) disable the
// cache, and this panel's DMA reads its framebuffer from octal PSRAM, which
// panics while the cache is off. The screen stays dark until reboot.
void display_suspend();
// Firmware updates on XIP builds: the panel keeps scanning while flash is
// written, but those writes share the memory bus with the PSRAM framebuffer.
// A slower pixel clock halves the scan-out's bandwidth so lines don't shift.
void display_update_mode(bool on);
bool display_is_suspended();
// True when the panel can keep drawing while flash is written without XIP:
// a panel with its own GRAM, fed by polling transfers that need no ISR
// (the Guition 3.5"). Updates then show their progress screen.
bool display_survives_flash();

// Page-slide compositor (unrotated panels only). Between begin and end LVGL's
// flushes are dropped and frames are built straight in the framebuffer from a
// capture of the screen plus a pre-rendered neighbor page, so a slide costs
// memory copies instead of re-rendering every widget each frame. All buffers
// are in PSRAM. Unavailable while the display is suspended.
bool display_can_compose();
// The panel's RGB565 framebuffer in native orientation (LCD_NATIVE_W x
// LCD_NATIVE_H), for /screenshot.png. nullptr while suspended.
const uint16_t* display_framebuffer();
uint16_t* display_page_buf(int slot);  // 2 full-screen RGB565 buffers
void display_compose_begin();          // capture the visible frame
// Current page shifted by dx (neighbor fills the gap: right of it when dx < 0,
// left when dx > 0); the keep areas are copied unshifted from the capture.
void display_compose(const uint16_t* neighbor, int dx, const lv_area_t* keep, int nkeep);
void display_compose_end();
