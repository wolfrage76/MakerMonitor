#pragma once
// Guition JC4880P443C (ESP32-P4): ST7701S 480x800 MIPI-DSI glass. The UI is
// an 800x480 landscape framebuffer; each frame is turned onto the portrait
// glass by the PPA and shown from a double-buffered DPI panel.
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t p4_dsi_init(void);
// Stops DPI scan-out before the OTA boot switch. Only a reboot restarts it.
void p4_dsi_stop(void);
// Rotate an 800x480 RGB565 frame onto the glass and show it. upside_down picks
// 90 instead of 270 degrees. The frame must be written back from the cache.
void p4_dsi_present(const uint16_t* logical, int w, int h, bool upside_down);

#ifdef __cplusplus
}
#endif
