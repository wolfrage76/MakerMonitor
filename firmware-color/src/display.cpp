// ESP32-S3 RGB panel driver + LVGL display driver for every board. The
// Waveshare 3.16" first needs its ST7701 init over bit-banged 3-wire SPI and
// is rotated to 820x320 landscape; the CrowPanels are landscape already.
#include "display.h"

#include <Arduino.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

#include "board.h"

static esp_lcd_panel_handle_t panel = nullptr;
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

// ---- RGB panel + LVGL ------------------------------------------------------

static void flushCb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  esp_lcd_panel_draw_bitmap(panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
  lv_disp_flush_ready(drv);
}

void display_set_brightness(uint8_t pct) {
  uint32_t duty = (uint32_t)pct * 255 / 100;
  ledcWrite(LCD_PIN_BL, LCD_BL_ACTIVE_HIGH ? duty : 255 - duty);
}

void display_init() {
  ledcAttach(LCD_PIN_BL, 20000, 8);
  display_set_brightness(0);

#ifdef LCD_PIN_PANEL_EN
  pinMode(LCD_PIN_PANEL_EN, OUTPUT);
  digitalWrite(LCD_PIN_PANEL_EN, LOW);
#endif
#if LCD_ST7701
  st7701Init();
#endif

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
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

  lv_init();
  auto* b1 = (lv_color_t*)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  auto* b2 = (lv_color_t*)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_disp_draw_buf_init(&drawBuf, b1, b2, BUF_PX);
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = LCD_NATIVE_W;
  dispDrv.ver_res = LCD_NATIVE_H;
  dispDrv.sw_rotate = LCD_ROTATION != LV_DISP_ROT_NONE;
  dispDrv.rotated = LCD_ROTATION;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);
}
