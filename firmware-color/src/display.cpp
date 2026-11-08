// ESP32-S3 RGB panel driver + LVGL display driver for every board. The
// Waveshare 3.16" first needs its ST7701 init over bit-banged 3-wire SPI and
// is rotated to 820x320 landscape; the CrowPanels are landscape already. The
// Guition 3.5" is a QSPI panel with its own GRAM instead: frames are kept in
// a PSRAM framebuffer and sent to the panel whole. The Waveshare 2" ST7789
// also keeps its picture; it takes dirty windows over plain SPI. The Guition
// 4.3" (ESP32-P4) keeps the same landscape PSRAM framebuffer and hands each
// whole frame to the PPA, which turns it onto the portrait DSI glass.
#include "display.h"

#include <Arduino.h>
#include "settings.h"
#include <algorithm>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

#include "board.h"
#include "ch422g.h"
#if LCD_CH422G
#include <Wire.h>
#endif
#if LCD_QSPI_AXS15231B || LCD_SPI_ST7789
#include <driver/spi_master.h>
#endif
#if LCD_MIPI_P4
#include "p4_dsi.h"
#endif

static esp_lcd_panel_handle_t panel = nullptr;
static bool displaySuspended = false;
static bool glassUpsideDown = false;

// The board's built-in orientation, plus 180 when the panel is mounted
// upside down. Panels with their own GRAM keep LVGL unrotated and turn the
// glass instead, so their framebuffer stays the picture a person sees.
#if !LCD_HAS_GRAM
static lv_disp_rot_t rotAdd180(lv_disp_rot_t r) {
  switch (r) {
    case LV_DISP_ROT_90: return LV_DISP_ROT_270;
    case LV_DISP_ROT_180: return LV_DISP_ROT_NONE;
    case LV_DISP_ROT_270: return LV_DISP_ROT_90;
    default: return LV_DISP_ROT_180;
  }
}
#endif

#if LCD_HAS_GRAM
static bool panelMadctlFlip() {
#if defined(LCD_FLIP)
  return (LCD_FLIP != 0) ^ glassUpsideDown;
#else
  return glassUpsideDown;
#endif
}
#endif

static lv_disp_rot_t activeRotation() {
#if LCD_HAS_GRAM
  return LV_DISP_ROT_NONE;
#else
  return glassUpsideDown ? rotAdd180(LCD_ROTATION) : LCD_ROTATION;
#endif
}

// Rotation LVGL itself applies. Touch boards keep this equal to the points
// they report, so a tap and a widget are the same numbers on every board.
// The 180 mount flip is done beside LVGL (pixels on the way out, touch on
// the way in). The 3.16" is the exception: it has no touch and is already
// turned 90 inside LVGL, so its flip is the other 90 (270).
static lv_disp_rot_t lvglRotation() {
#if LCD_HAS_GRAM
  return LV_DISP_ROT_NONE;
#else
  if (LCD_ROTATION == LV_DISP_ROT_NONE) return LV_DISP_ROT_NONE;
  return activeRotation();
#endif
}

bool display_upside_down() { return glassUpsideDown; }
static lv_disp_draw_buf_t drawBuf;
static lv_disp_drv_t dispDrv;
// LVGL draw buffers (internal RAM, two of them): 40 lines of 320 px.
static const size_t BUF_PX = 320 * 40;

#if LCD_ST7701
// ---- ST7701 init (9-bit SPI: D/C bit then 8 data bits, MSB first) --------

static void spiWrite9(bool data, uint8_t b) {
  uint16_t word = (data ? 0x100 : 0) | b;
  for (int i = 8; i >= 0; i--) {
    digitalWrite(LCD_PIN_SCK, LOW);
    digitalWrite(LCD_PIN_SDA, (word >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(LCD_PIN_SCK, HIGH);
    delayMicroseconds(1);
  }
}

static void lcdCmd(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0) {
  digitalWrite(LCD_PIN_CS, LOW);
  spiWrite9(false, cmd);
  for (size_t i = 0; i < len; i++) spiWrite9(true, data[i]);
  digitalWrite(LCD_PIN_CS, HIGH);
}

struct InitCmd {
  uint8_t cmd;
  uint8_t len;
  uint8_t data[16];
  uint16_t delayMs;
};

// From a working ESPHome config for this board. If the screen stays blank or
// garbled, compare with Waveshare's 07_LVGL demo and replace this table.
static const InitCmd INIT_SEQ[] = {
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13}, 0},
  {0xEF, 1, {0x08}, 0},
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x10}, 0},
  {0xC0, 2, {0xE5, 0x02}, 0},
  {0xC1, 2, {0x15, 0x0A}, 0},
  {0xC2, 2, {0x07, 0x02}, 0},
  {0xCC, 1, {0x10}, 0},
  {0xB0, 16, {0x00, 0x08, 0x51, 0x0D, 0xCE, 0x06, 0x00, 0x08, 0x08, 0x24, 0x05, 0xD0, 0x0F, 0x6F, 0x36, 0x1F}, 0},
  {0xB1, 16, {0x00, 0x10, 0x4F, 0x0C, 0x11, 0x05, 0x00, 0x07, 0x07, 0x18, 0x02, 0xD3, 0x11, 0x6E, 0x34, 0x1F}, 0},
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x11}, 0},
  {0xB0, 1, {0x4D}, 0},
  {0xB1, 1, {0x37}, 0},
  {0xB2, 1, {0x87}, 0},
  {0xB3, 1, {0x80}, 0},
  {0xB5, 1, {0x4A}, 0},
  {0xB7, 1, {0x85}, 0},
  {0xB8, 1, {0x21}, 0},
  {0xB9, 2, {0x00, 0x13}, 0},
  {0xC0, 1, {0x09}, 0},
  {0xC1, 1, {0x78}, 0},
  {0xC2, 1, {0x78}, 0},
  {0xD0, 1, {0x88}, 0},
  {0xE0, 3, {0x80, 0x00, 0x02}, 100},
  {0xE1, 11, {0x0F, 0xA0, 0x00, 0x00, 0x10, 0xA0, 0x00, 0x00, 0x00, 0x60, 0x60}, 0},
  {0xE2, 13, {0x30, 0x30, 0x60, 0x60, 0x45, 0xA0, 0x00, 0x00, 0x46, 0xA0, 0x00, 0x00, 0x00}, 0},
  {0xE3, 4, {0x00, 0x00, 0x33, 0x33}, 0},
  {0xE4, 2, {0x44, 0x44}, 0},
  {0xE5, 16, {0x0F, 0x4A, 0xA0, 0xA0, 0x11, 0x4A, 0xA0, 0xA0, 0x13, 0x4A, 0xA0, 0xA0, 0x15, 0x4A, 0xA0, 0xA0}, 0},
  {0xE6, 4, {0x00, 0x00, 0x33, 0x33}, 0},
  {0xE7, 2, {0x44, 0x44}, 0},
  {0xE8, 16, {0x10, 0x4A, 0xA0, 0xA0, 0x12, 0x4A, 0xA0, 0xA0, 0x14, 0x4A, 0xA0, 0xA0, 0x16, 0x4A, 0xA0, 0xA0}, 0},
  {0xEB, 7, {0x02, 0x00, 0x4E, 0x4E, 0xEE, 0x44, 0x00}, 0},
  {0xED, 16, {0xFF, 0xFF, 0x04, 0x56, 0x72, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x27, 0x65, 0x40, 0xFF, 0xFF}, 0},
  {0xEF, 6, {0x08, 0x08, 0x08, 0x40, 0x3F, 0x64}, 0},
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13}, 0},
  {0xE8, 2, {0x00, 0x0E}, 0},
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00}, 0},
  {0x11, 0, {0}, 120},
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13}, 0},
  {0xE8, 2, {0x00, 0x0C}, 10},
  {0xE8, 2, {0x00, 0x00}, 0},
  {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00}, 0},
  {0x3A, 1, {0x55}, 0},
  {0x36, 1, {0x00}, 0},
  {0x35, 1, {0x00}, 0},
  {0x29, 0, {0}, 20},
};

static void st7701Init() {
  pinMode(LCD_PIN_CS, OUTPUT);
  pinMode(LCD_PIN_SCK, OUTPUT);
  pinMode(LCD_PIN_SDA, OUTPUT);
  pinMode(LCD_PIN_RST, OUTPUT);
  digitalWrite(LCD_PIN_CS, HIGH);
  digitalWrite(LCD_PIN_SCK, HIGH);
  digitalWrite(LCD_PIN_RST, HIGH);
  delay(10);
  digitalWrite(LCD_PIN_RST, LOW);
  delay(10);
  digitalWrite(LCD_PIN_RST, HIGH);
  delay(120);
  for (const InitCmd& c : INIT_SEQ) {
    lcdCmd(c.cmd, c.data, c.len);
    if (c.delayMs) delay(c.delayMs);
  }
  // CS is shared with the BOOT button / knob switch; release it.
  pinMode(LCD_PIN_CS, INPUT_PULLUP);
}
#endif

#if LCD_QSPI_AXS15231B
// ---- AXS15231B over QSPI ---------------------------------------------------
// Commands go out single-line as 0x02 + (cmd << 8); pixels as 0x32 + 0x3C00
// with quad data. The panel ignores RASET in this mode, so every frame is
// sent whole from row 0, rotated from the landscape framebuffer to the
// portrait glass and byte-swapped on the way. Polling transfers only: no
// completion callback has to stay out of flash while flash is written.

static spi_device_handle_t qspi = nullptr;
static uint16_t* txBuf[2] = {nullptr, nullptr};
static const int TX_ROWS = 16;  // panel rows per DMA chunk
static uint16_t* qspiFb = nullptr;

struct AxsCmd {
  uint8_t cmd;
  uint8_t len;
  uint8_t data[31];
  uint16_t delayMs;
};

// Arduino_GFX's table for this panel (proven on the JC3248W535).
static const AxsCmd AXS_INIT[] = {
  {0xBB, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5}, 0},
  {0xA0, 17, {0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00}, 0},
  {0xA2, 31, {0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0, 0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xF9, 0x10, 0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0, 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A}, 0},
  {0xD0, 30, {0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15, 0xC2, 0x42, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12}, 0},
  {0xA3, 22, {0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55}, 0},
  {0xC1, 30, {0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40}, 0},
  {0xC3, 11, {0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01}, 0},
  {0xC4, 29, {0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32, 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50}, 0},
  {0xC5, 23, {0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20, 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00}, 0},
  {0xC6, 20, {0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22}, 0},
  {0xC7, 20, {0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00, 0x80, 0xFF, 0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24, 0x0C, 0x0D, 0x0E, 0x0F}, 0},
  {0xC9, 4, {0x33, 0x44, 0x44, 0x01}, 0},
  {0xCF, 27, {0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08}, 0},
  {0xD5, 30, {0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00}, 0},
  {0xD6, 30, {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00}, 0},
  {0xD7, 19, {0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F, 0x18, 0x1D, 0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F}, 0},
  {0xD8, 12, {0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F, 0x18, 0x1D, 0x1F, 0x19}, 0},
  {0xD9, 12, {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 0},
  {0xDD, 12, {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 0},
  {0xDF, 8, {0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90}, 0},
  {0xE0, 17, {0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D}, 0},
  {0xE1, 17, {0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F}, 0},
  {0xE2, 17, {0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 0},
  {0xE3, 17, {0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F}, 0},
  {0xE4, 17, {0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 0},
  {0xE5, 17, {0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F}, 0},
  {0xA4, 16, {0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30}, 0},
  {0xA4, 4, {0x85, 0x85, 0x95, 0x85}, 0},
  {0xBB, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0},
  {0x11, 1, {0x00}, 120},
  {0x29, 1, {0x00}, 100},
  {0x2C, 4, {0x00, 0x00, 0x00, 0x00}, 0},
};

static inline void csLow() { digitalWrite(LCD_PIN_CS, LOW); }
static inline void csHigh() { digitalWrite(LCD_PIN_CS, HIGH); }

static void axsCmd(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0) {
  spi_transaction_t t = {};
  t.cmd = 0x02;
  t.addr = (uint32_t)cmd << 8;
  if (len && len <= 4) {
    t.flags = SPI_TRANS_USE_TXDATA;
    memcpy(t.tx_data, data, len);
  } else if (len) {
    t.tx_buffer = data;
  }
  t.length = len * 8;
  csLow();
  spi_device_polling_transmit(qspi, &t);
  csHigh();
}

// Panel rows [py0, py0 + TX_ROWS) from the landscape framebuffer. Read
// landscape row by landscape row so each pass stays within a cache line.
static void axsFill(uint16_t* out, int py0) {
  const int W = LCD_NATIVE_W, PW = LCD_PANEL_W;
  for (int px = 0; px < PW; px++) {
    if (panelMadctlFlip()) {
      const uint16_t* src = qspiFb + px * W + (W - 1 - py0);
      for (int k = 0; k < TX_ROWS; k++) out[k * PW + px] = __builtin_bswap16(src[-k]);
    } else {
      const uint16_t* src = qspiFb + (PW - 1 - px) * W + py0;
      for (int k = 0; k < TX_ROWS; k++) out[k * PW + px] = __builtin_bswap16(src[k]);
    }
  }
}

static void axsPush() {
  if (!qspi || displaySuspended) return;
  static const uint8_t caset[4] = {0, 0, (LCD_PANEL_W - 1) >> 8, (LCD_PANEL_W - 1) & 0xFF};
  static const uint8_t raset[4] = {0, 0, (LCD_PANEL_H - 1) >> 8, (LCD_PANEL_H - 1) & 0xFF};
  axsCmd(0x2A, caset, 4);
  axsCmd(0x2B, raset, 4);
  axsCmd(0x2C);
  static spi_transaction_ext_t t[2];
  csLow();
  for (int c = 0; c < LCD_PANEL_H / TX_ROWS; c++) {
    uint16_t* buf = txBuf[c & 1];
    axsFill(buf, c * TX_ROWS);
    if (c) spi_device_polling_end(qspi, portMAX_DELAY);
    spi_transaction_ext_t& x = t[c & 1];
    x = {};
    if (c == 0) {
      x.base.flags = SPI_TRANS_MODE_QIO;
      x.base.cmd = 0x32;
      x.base.addr = 0x003C00;
    } else {
      // Same CS-low run: the rest of the frame has no command or address.
      x.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    }
    x.base.tx_buffer = buf;
    x.base.length = TX_ROWS * LCD_PANEL_W * 16;
    spi_device_polling_start(qspi, &x.base, portMAX_DELAY);
  }
  spi_device_polling_end(qspi, portMAX_DELAY);
  csHigh();
}

static void axsInit() {
  pinMode(LCD_PIN_CS, OUTPUT);
  csHigh();
  spi_bus_config_t bus = {};
  bus.data0_io_num = LCD_PIN_D0;
  bus.data1_io_num = LCD_PIN_D1;
  bus.sclk_io_num = LCD_PIN_SCK;
  bus.data2_io_num = LCD_PIN_D2;
  bus.data3_io_num = LCD_PIN_D3;
  bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
  bus.max_transfer_sz = TX_ROWS * LCD_PANEL_W * 2 + 8;
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
  spi_device_interface_config_t dev = {};
  dev.command_bits = 8;
  dev.address_bits = 24;
  dev.mode = 0;
  dev.clock_speed_hz = LCD_SPI_HZ;
  dev.spics_io_num = -1;  // CS stays low across a whole frame
  dev.flags = SPI_DEVICE_HALFDUPLEX;
  dev.queue_size = 1;
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &qspi));
  spi_device_acquire_bus(qspi, portMAX_DELAY);
  for (auto& b : txBuf)
    b = (uint16_t*)heap_caps_aligned_alloc(16, TX_ROWS * LCD_PANEL_W * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

  axsCmd(0x01);  // no reset line: software reset
  delay(200);
  for (const AxsCmd& c : AXS_INIT) {
    axsCmd(c.cmd, c.data, c.len);
    if (c.delayMs) delay(c.delayMs);
  }
  axsCmd(0x20);  // INVOFF
  const uint8_t madctl = 0x00;
  axsCmd(0x36, &madctl, 1);
}
#endif

#if LCD_SPI_ST7789
// ---- ST7789 over 4-wire SPI --------------------------------------------------
// D/C and CS by hand, polling transfers only (nothing has to stay out of
// flash). The panel turns itself to landscape (MADCTL MV) and honours
// CASET/RASET, so a flush sends just its window from the framebuffer,
// byte-swapped on the way.

static spi_device_handle_t lspi = nullptr;
static uint16_t* txBuf[2] = {nullptr, nullptr};
static const int TX_PX = 16 * LCD_NATIVE_W;  // pixels per DMA chunk
static uint16_t* stFb = nullptr;

static void stSend(bool data, const void* buf, size_t len) {
  digitalWrite(LCD_PIN_DC, data);
  spi_transaction_t t = {};
  t.length = len * 8;
  if (len <= 4) {
    t.flags = SPI_TRANS_USE_TXDATA;
    memcpy(t.tx_data, buf, len);
  } else {
    t.tx_buffer = buf;
  }
  spi_device_polling_transmit(lspi, &t);
}

static void stCmd(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0) {
  digitalWrite(LCD_PIN_CS, LOW);
  stSend(false, &cmd, 1);
  if (len) stSend(true, data, len);
  digitalWrite(LCD_PIN_CS, HIGH);
}

static void stPush(int x1, int y1, int x2, int y2) {
  if (!lspi || displaySuspended) return;
  const uint8_t ca[4] = {(uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2};
  const uint8_t ra[4] = {(uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2};
  stCmd(0x2A, ca, 4);
  stCmd(0x2B, ra, 4);
  const int w = x2 - x1 + 1, rows = max(1, TX_PX / w);
  static spi_transaction_t t[2];
  digitalWrite(LCD_PIN_CS, LOW);
  const uint8_t ramwr = 0x2C;
  stSend(false, &ramwr, 1);
  digitalWrite(LCD_PIN_DC, HIGH);
  int n = 0;
  for (int y = y1; y <= y2; y += rows, n++) {
    const int h = min(rows, y2 - y + 1);
    uint16_t* buf = txBuf[n & 1];
    for (int r = 0; r < h; r++) {
      const uint16_t* src = stFb + (y + r) * LCD_NATIVE_W + x1;
      uint16_t* dst = buf + r * w;
      for (int i = 0; i < w; i++) dst[i] = __builtin_bswap16(src[i]);
    }
    if (n) spi_device_polling_end(lspi, portMAX_DELAY);
    t[n & 1] = {};
    t[n & 1].tx_buffer = buf;
    t[n & 1].length = h * w * 16;
    spi_device_polling_start(lspi, &t[n & 1], portMAX_DELAY);
  }
  if (n) spi_device_polling_end(lspi, portMAX_DELAY);
  digitalWrite(LCD_PIN_CS, HIGH);
}

static void stPushAll() { stPush(0, 0, LCD_NATIVE_W - 1, LCD_NATIVE_H - 1); }

static void stApplyFlip() {
  if (!lspi) return;
  const uint8_t madctl = panelMadctlFlip() ? 0xA0 : 0x60;  // MV + MY or MX: landscape
  stCmd(0x36, &madctl, 1);
  stPushAll();
}

static void stInit() {
  pinMode(LCD_PIN_CS, OUTPUT);
  pinMode(LCD_PIN_DC, OUTPUT);
  digitalWrite(LCD_PIN_CS, HIGH);
  pinMode(LCD_PIN_RST, OUTPUT);
  digitalWrite(LCD_PIN_RST, LOW);
  delay(10);
  digitalWrite(LCD_PIN_RST, HIGH);
  delay(120);
  spi_bus_config_t bus = {};
  bus.mosi_io_num = LCD_PIN_MOSI;
  bus.miso_io_num = -1;
  bus.sclk_io_num = LCD_PIN_SCK;
  bus.quadwp_io_num = bus.quadhd_io_num = -1;
  bus.max_transfer_sz = TX_PX * 2;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
  spi_device_interface_config_t dev = {};
  dev.mode = 0;
  dev.clock_speed_hz = LCD_SPI_HZ;
  dev.spics_io_num = -1;
  dev.queue_size = 1;
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &lspi));
  spi_device_acquire_bus(lspi, portMAX_DELAY);
  for (auto& b : txBuf) b = (uint16_t*)heap_caps_aligned_alloc(16, TX_PX * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

  stCmd(0x01);  // SWRESET
  delay(150);
  stCmd(0x11);  // SLPOUT
  delay(120);
  const uint8_t colmod = 0x55;  // RGB565
  stCmd(0x3A, &colmod, 1);
  const uint8_t madctl = panelMadctlFlip() ? 0xA0 : 0x60;  // MV + MY or MX: landscape
  stCmd(0x36, &madctl, 1);
  stCmd(0x21);  // INVON: IPS glass
  stCmd(0x13);  // NORON
  delay(10);
  stCmd(0x29);  // DISPON
  delay(20);
}
#endif

#if LCD_MIPI_P4
// ---- Guition 4.3" (ESP32-P4, MIPI-DSI) --------------------------------------
// The PPA reads p4Fb by DMA, so the CPU cache is written back first. Presents
// are at least P4_GAP_US apart: back-to-back full-frame rotates on the PSRAM
// bus the glass is scanning froze the picture on the Digital Swatch port.
static uint16_t* p4Fb = nullptr;
static bool p4Stopped = false;
static uint32_t p4LastUs = 0;
static const uint32_t P4_GAP_US = 16000;

static void p4Push() {
  if (!p4Fb || p4Stopped) return;
  esp_cache_msync(p4Fb, LCD_NATIVE_W * LCD_NATIVE_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  uint32_t since = micros() - p4LastUs;
  if (p4LastUs && since < P4_GAP_US) delayMicroseconds(P4_GAP_US - since);
  p4LastUs = micros();
  p4_dsi_present(p4Fb, LCD_NATIVE_W, LCD_NATIVE_H, panelMadctlFlip());
}
#endif

// ---- RGB panel + LVGL ------------------------------------------------------

static bool composing = false;

// RGB boards have a panel handle; the QSPI board has its own framebuffer.
static bool panelLive() {
#if LCD_QSPI_AXS15231B
  return qspiFb != nullptr;
#elif LCD_SPI_ST7789
  return stFb != nullptr;
#elif LCD_MIPI_P4
  return p4Fb != nullptr && !p4Stopped;
#else
  return panel != nullptr;
#endif
}

// ---- software dimming (on/off backlights) ------------------------------------
// A lookup table per 5- and 6-bit channel, scaled from 100% down to a floor
// of 25%: below that colours run together (same floor as SpoolmanScale).
static bool dimOn = false;
static uint8_t dimLut5[32], dimLut6[64];

static void dimBuild(int pct) {
  dimOn = pct < 100;
  if (!dimOn) return;
  float k = 0.25f + 0.75f * pct / 100.0f;
  for (int i = 0; i < 32; i++) dimLut5[i] = (uint8_t)(i * k + 0.5f);
  for (int i = 0; i < 64; i++) dimLut6[i] = (uint8_t)(i * k + 0.5f);
}

void display_dim_pixels(uint16_t* px, size_t n) {
  if (!dimOn) return;
  for (size_t i = 0; i < n; i++) {
    uint16_t c = px[i];
    px[i] = (dimLut5[c >> 11] << 11) | (dimLut6[(c >> 5) & 0x3F] << 5) | dimLut5[c & 0x1F];
  }
}

static void flushCb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  if (dimOn) display_dim_pixels((uint16_t*)px, (size_t)(a->x2 - a->x1 + 1) * (a->y2 - a->y1 + 1));
#if LCD_QSPI_AXS15231B
  if (qspiFb && !displaySuspended && !composing) {
    const int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; y++)
      memcpy(qspiFb + y * LCD_NATIVE_W + a->x1, (const uint16_t*)px + (y - a->y1) * w, w * 2);
    if (lv_disp_flush_is_last(drv)) axsPush();
  }
#elif LCD_SPI_ST7789
  if (stFb && !displaySuspended && !composing) {
    const int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; y++)
      memcpy(stFb + y * LCD_NATIVE_W + a->x1, (const uint16_t*)px + (y - a->y1) * w, w * 2);
    stPush(a->x1, a->y1, a->x2, a->y2);
  }
#elif LCD_MIPI_P4
  if (p4Fb && !displaySuspended && !composing) {
    const int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; y++)
      memcpy(p4Fb + y * LCD_NATIVE_W + a->x1, (const uint16_t*)px + (y - a->y1) * w, w * 2);
    if (lv_disp_flush_is_last(drv)) p4Push();
  }
#else
  if (panel && !displaySuspended && !composing) {
    int x = a->x1, y = a->y1;
    const int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
    // Landscape RGB panels (CrowPanels, Waveshare 7"): turn a 180 mount
    // here, not in LVGL, so touch stays in widget coordinates.
    if (glassUpsideDown && LCD_ROTATION == LV_DISP_ROT_NONE) {
      std::reverse(reinterpret_cast<uint16_t*>(px), reinterpret_cast<uint16_t*>(px) + (size_t)w * h);
      x = LCD_NATIVE_W - 1 - a->x2;
      y = LCD_NATIVE_H - 1 - a->y2;
    }
    esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, px);
  }
#endif
  lv_disp_flush_ready(drv);
}

void display_suspend() {
  if (displaySuspended) return;
  displaySuspended = true;
  display_set_brightness(0);
#if LCD_MIPI_P4
  // Called from the OTA boot switch too: DPI DMA must be quiet before the
  // flash is remapped.
  p4Stopped = true;
  p4_dsi_stop();
#endif
  // The QSPI panel holds its own picture; just stop sending frames.
  if (!panel) return;
  esp_lcd_panel_handle_t dying = panel;
  panel = nullptr;
  esp_lcd_panel_del(dying);
}

bool display_is_suspended() { return displaySuspended; }

bool display_survives_flash() { return LCD_HAS_GRAM; }

void deviceLogf(const char* fmt, ...);

void display_update_mode(bool on) {
#if LCD_HAS_GRAM
  (void)on;  // no pixel clock: the panel refreshes from its own GRAM
#else
  if (!panel) return;
  uint32_t hz = on ? LCD_PCLK_HZ / 2 : LCD_PCLK_HZ;
  esp_err_t err = esp_lcd_rgb_panel_set_pclk(panel, hz);  // applied at the next VSYNC
  deviceLogf("Pixel clock %lu Hz for %s: %s", (unsigned long)hz, on ? "update" : "normal", esp_err_to_name(err));
#endif
}

// ---- page-slide compositor -------------------------------------------------

static uint16_t* fb = nullptr;       // the panel's framebuffer
static uint16_t* capture = nullptr;  // what was on screen when a slide began
static uint16_t* pages[2] = {nullptr, nullptr};

static uint16_t* fbAny = nullptr;  // framebuffer on every panel, rotated or not

const uint16_t* display_framebuffer() {
  if (!panelLive() || displaySuspended) return nullptr;
#if LCD_MIPI_P4
  // Every frame was written back before the PPA read it, so dropping the
  // cached copy is safe; screenshots then read what the PPA rotated (the
  // Digital Swatch port saw a stale band without this).
  esp_cache_msync(fbAny, LCD_NATIVE_W * LCD_NATIVE_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
#endif
  return fbAny;
}

void display_view_size(int& w, int& h) {
  // LVGL's 90/270 rotation swaps the driver's native axes. 180 keeps them.
  // GRAM boards report NONE here: their framebuffer is already the view.
  const lv_disp_rot_t r = activeRotation();
  if (r == LV_DISP_ROT_90 || r == LV_DISP_ROT_270) {
    w = LCD_NATIVE_H;
    h = LCD_NATIVE_W;
  } else {
    w = LCD_NATIVE_W;
    h = LCD_NATIVE_H;
  }
}

uint16_t display_view_pixel(const uint16_t* fb, int x, int y) {
  // Inverse of LVGL's software rotation (lv_refr.c draw_buf_rotate), so a
  // screenshot is the picture on the glass. ROT_90 was checked against a
  // 3.16" capture: native (px, py) = (ly, native_h - 1 - lx).
  const int NW = LCD_NATIVE_W, NH = LCD_NATIVE_H;
  int px = x, py = y;
  switch (activeRotation()) {
    case LV_DISP_ROT_90:
      px = y;
      py = NH - 1 - x;
      break;
    case LV_DISP_ROT_270:
      px = NW - 1 - y;
      py = x;
      break;
    case LV_DISP_ROT_180:
      px = NW - 1 - x;
      py = NH - 1 - y;
      break;
    default:
      break;
  }
  return fb[(size_t)py * NW + px];
}

void display_set_upside_down(bool on) {
  if (glassUpsideDown == on) return;
  glassUpsideDown = on;
#if LCD_SPI_ST7789
  stApplyFlip();
#endif
#if LCD_HAS_GRAM
  if (lv_disp_get_default()) {
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
  }
#else
  if (LCD_ROTATION == LV_DISP_ROT_NONE) {
    // Mirror happens in the flush. A full redraw picks it up.
    if (lv_disp_get_default()) {
      lv_obj_invalidate(lv_scr_act());
      lv_obj_invalidate(lv_layer_top());
    }
  } else {
    // 3.16": no touch, and the picture is already turned inside LVGL.
    lv_disp_rot_t rot = lvglRotation();
    dispDrv.sw_rotate = rot != LV_DISP_ROT_NONE;
    if (lv_disp_get_default()) lv_disp_set_rotation(nullptr, rot);
    else dispDrv.rotated = rot;
  }
#endif
}

bool display_can_compose() {
  // A flipped landscape RGB panel mirrors in the flush. Composing straight
  // into the scan-out buffer would skip that, so slides fall back to LVGL.
  // The 3.16" is turned inside LVGL and never composes.
  return fb && capture && pages[0] && pages[1] && panelLive() && !displaySuspended &&
         activeRotation() == LV_DISP_ROT_NONE;
}

uint16_t* display_page_buf(int slot) { return pages[slot]; }

void display_compose_begin() {
  composing = true;
  memcpy(capture, fb, LCD_NATIVE_W * LCD_NATIVE_H * 2);
}

void display_compose(const uint16_t* nb, int dx, const lv_area_t* keep, int nkeep) {
  if (!display_can_compose()) return;
  const int W = LCD_NATIVE_W, H = LCD_NATIVE_H;
  dx = constrain(dx, -W, W);
  for (int y = 0; y < H; y++) {
    uint16_t* d = fb + y * W;
    const uint16_t* c = capture + y * W;
    const uint16_t* n = nb + y * W;
    if (dx <= 0) {  // next page comes in from the right
      memcpy(d, c - dx, (W + dx) * 2);
      if (dx) memcpy(d + W + dx, n, -dx * 2);
    } else {        // previous page comes in from the left
      memcpy(d, n + W - dx, dx * 2);
      memcpy(d + dx, c, (W - dx) * 2);
    }
  }
  for (int i = 0; i < nkeep; i++) {
    const lv_area_t& k = keep[i];
    int x1 = max<int>(0, k.x1), x2 = min<int>(W - 1, k.x2);
    for (int y = max<int>(0, k.y1); y <= min<int>(H - 1, k.y2) && x2 >= x1; y++)
      memcpy(fb + y * W + x1, capture + y * W + x1, (x2 - x1 + 1) * 2);
  }
#if LCD_QSPI_AXS15231B
  axsPush();
#elif LCD_SPI_ST7789
  stPushAll();
#elif LCD_MIPI_P4
  p4Push();
#else
  // The RGB DMA reads PSRAM directly; push the CPU cache out.
  esp_cache_msync(fb, W * H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#endif
}

void display_compose_end() { composing = false; }

void display_set_brightness(uint8_t pct) {
#if LCD_CH422G || LCD_BL_ONOFF
  // The expander can only switch the backlight on or off, and PWM on the 2"
  // backlight makes ghost touches; levels in between darken the picture
  // instead, and the whole screen is drawn again.
  static int last = -1;
  if ((pct > 0) != (last > 0) || last < 0) {
#if LCD_CH422G
    ch422g_write(CH422G_BACKLIGHT, pct > 0);
#else
    digitalWrite(LCD_PIN_BL, (pct > 0) == LCD_BL_ACTIVE_HIGH);
#endif
  }
  if (pct > 0 && pct != last) {
    bool was = dimOn;
    dimBuild(pct);
    if ((dimOn || was) && lv_disp_get_default()) {
      lv_obj_invalidate(lv_scr_act());
      lv_obj_invalidate(lv_layer_top());
    }
  }
  last = pct;
#else
  // Some backlight drivers stay dark below a minimum duty: spread 1-100%
  // over LCD_BL_MIN_PCT-100% so the dim levels (3-10%) still light up.
  if (pct > 0 && LCD_BL_MIN_PCT > 0) pct = LCD_BL_MIN_PCT + (uint32_t)min<uint8_t>(pct, 100) * (100 - LCD_BL_MIN_PCT) / 100;
  uint32_t duty = (uint32_t)pct * 255 / 100;
  ledcWrite(LCD_PIN_BL, LCD_BL_ACTIVE_HIGH ? duty : 255 - duty);
#endif
}

void display_init() {
  glassUpsideDown = setting_value(SET_FLIP) != 0;
#if LCD_CH422G
  // Everything held low (panel and touch in reset, backlight off), then the
  // LCD out of reset before the RGB driver starts.
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  if (!ch422g_begin(0x00)) Serial.println("CH422G does not answer");
  ch422g_write(CH422G_LCD_RST, true);
  delay(20);
#elif LCD_BL_ONOFF
  pinMode(LCD_PIN_BL, OUTPUT);
#else
  ledcAttach(LCD_PIN_BL, 20000, 8);
#endif
  display_set_brightness(0);

#ifdef LCD_PIN_PANEL_EN
  pinMode(LCD_PIN_PANEL_EN, OUTPUT);
  digitalWrite(LCD_PIN_PANEL_EN, LOW);
#endif
#if LCD_ST7701
  st7701Init();
#endif

#if LCD_QSPI_AXS15231B
  axsInit();
  qspiFb = (uint16_t*)heap_caps_calloc(LCD_NATIVE_W * LCD_NATIVE_H, 2, MALLOC_CAP_SPIRAM);
  void* fbp = qspiFb;
  fbAny = qspiFb;
  axsPush();  // clear whatever the panel's GRAM held
#elif LCD_SPI_ST7789
  stInit();
  stFb = (uint16_t*)heap_caps_calloc(LCD_NATIVE_W * LCD_NATIVE_H, 2, MALLOC_CAP_SPIRAM);
  void* fbp = stFb;
  fbAny = stFb;
  stPushAll();  // clear whatever the panel's GRAM held
#elif LCD_MIPI_P4
  if (p4_dsi_init() != ESP_OK) Serial.println("P4 DSI panel did not start");
  // 64-byte aligned: the cache write-back before each PPA rotate works in lines.
  p4Fb = (uint16_t*)heap_caps_aligned_calloc(64, LCD_NATIVE_W * LCD_NATIVE_H, 2, MALLOC_CAP_SPIRAM);
  void* fbp = p4Fb;
  fbAny = p4Fb;
  p4Push();
#else
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = LCD_PCLK_HZ;
  cfg.timings.h_res = LCD_NATIVE_W;
  cfg.timings.v_res = LCD_NATIVE_H;
  cfg.timings.hsync_pulse_width = LCD_HSYNC_PW;
  cfg.timings.hsync_back_porch = LCD_HSYNC_BP;
  cfg.timings.hsync_front_porch = LCD_HSYNC_FP;
  cfg.timings.vsync_pulse_width = LCD_VSYNC_PW;
  cfg.timings.vsync_back_porch = LCD_VSYNC_BP;
  cfg.timings.vsync_front_porch = LCD_VSYNC_FP;
  cfg.timings.flags.pclk_active_neg = LCD_PCLK_NEG;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 1;
  cfg.bounce_buffer_size_px = LCD_BOUNCE_LINES * LCD_NATIVE_W;
  cfg.hsync_gpio_num = LCD_PIN_HSYNC;
  cfg.vsync_gpio_num = LCD_PIN_VSYNC;
  cfg.de_gpio_num = LCD_PIN_DE;
  cfg.pclk_gpio_num = LCD_PIN_PCLK;
  cfg.disp_gpio_num = -1;
  const int pins[16] = LCD_DATA_PINS;
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = pins[i];
  cfg.flags.fb_in_psram = 1;
  // Bounce mode: drop each framebuffer line from the cache once copied, so
  // scan-out doesn't evict LVGL's working set (everything that writes the
  // framebuffer writes its cache back first).
  cfg.flags.bb_invalidate_cache = LCD_BOUNCE_LINES > 0;
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

  // Unrotated panels: LVGL and framebuffer coordinates match, so slides can
  // be composed directly (3 extra full frames, all in PSRAM).
  void* fbp = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fbp) == ESP_OK) fbAny = (uint16_t*)fbp;
#endif
  if (LCD_ROTATION == LV_DISP_ROT_NONE && fbAny) {
    fb = (uint16_t*)fbp;
    const size_t sz = LCD_NATIVE_W * LCD_NATIVE_H * 2;
    capture = (uint16_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    pages[0] = (uint16_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    pages[1] = (uint16_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  }

  lv_init();
  auto* b1 = (lv_color_t*)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  auto* b2 = (lv_color_t*)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_disp_draw_buf_init(&drawBuf, b1, b2, BUF_PX);
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = LCD_NATIVE_W;
  dispDrv.ver_res = LCD_NATIVE_H;
  dispDrv.rotated = lvglRotation();
  dispDrv.sw_rotate = dispDrv.rotated != LV_DISP_ROT_NONE;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);
}
