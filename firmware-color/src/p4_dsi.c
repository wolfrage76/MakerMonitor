/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 ultramcu
 *
 * Display bring-up from guition-jc4880p4-bsp
 * (https://github.com/ultramcu/guition-jc4880p4-bsp), trimmed to what
 * MakerMonitor uses: DSI panel, PPA rotation and stop. The backlight PWM and
 * GT911 touch are MakerMonitor's own (display.cpp, touch.cpp).
 */
// Every board env compiles all of src/, so this file is empty except on the P4.
#if defined(BOARD_JC4880P443)

#include "p4_dsi.h"

#include <string.h>
#include "esp_cache.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ppa.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "hal/axi_icm_ll.h"
#include "hal/mipi_dsi_types.h"

#include "p4_st7701_init.h"

#define P4_LCD_H_RES 480
#define P4_LCD_V_RES 800
#define P4_LCD_RST 5
#define P4_DSI_LDO_CHAN 3
#define P4_DSI_LDO_MV 2500
#define P4_DSI_LANES 2
#define P4_DSI_LANE_MBPS 500
// 34 MHz with the link dropping to low power in blanking, as Guition ships it.
// Keeping the clock running makes the ST7701 count a different line length and
// the picture shears.
#define P4_DPI_CLK_MHZ 34

static const char* TAG = "p4_dsi";

static esp_ldo_channel_handle_t s_ldo = NULL;
static esp_lcd_dsi_bus_handle_t s_bus = NULL;
static esp_lcd_panel_io_handle_t s_dbi = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t* s_fb[2] = {NULL, NULL};
static int s_back = 1;
static ppa_client_handle_t s_ppa = NULL;

static void panel_reset(void) {
  gpio_config_t rst = {.pin_bit_mask = 1ULL << P4_LCD_RST, .mode = GPIO_MODE_OUTPUT};
  gpio_config(&rst);
  gpio_set_level(P4_LCD_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(P4_LCD_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(120));
}

esp_err_t p4_dsi_init(void) {
  esp_ldo_channel_config_t ldo = {.chan_id = P4_DSI_LDO_CHAN, .voltage_mv = P4_DSI_LDO_MV};
  ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo, &s_ldo), TAG, "DSI LDO");

  // Rev 3 silicon aborts on the legacy PLL_F20M source; DEFAULT is XTAL there.
  esp_lcd_dsi_bus_config_t bus = {
      .bus_id = 0,
      .num_data_lanes = P4_DSI_LANES,
      .phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT,
      .lane_bit_rate_mbps = P4_DSI_LANE_MBPS,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus, &s_bus), TAG, "DSI bus");

  esp_lcd_dbi_io_config_t dbi = {.virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_bus, &dbi, &s_dbi), TAG, "DBI io");

  // Two framebuffers: the PPA writes the hidden one, then the DPI switches to it.
  esp_lcd_dpi_panel_config_t dpi = {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = P4_DPI_CLK_MHZ,
      .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
      .in_color_format = LCD_COLOR_FMT_RGB565,
      .out_color_format = LCD_COLOR_FMT_RGB565,
      .num_fbs = 2,
      .video_timing =
          {
              .h_size = P4_LCD_H_RES,
              .v_size = P4_LCD_V_RES,
              .hsync_pulse_width = 12,
              .hsync_back_porch = 42,
              .hsync_front_porch = 42,
              .vsync_pulse_width = 2,
              .vsync_back_porch = 8,
              .vsync_front_porch = 166,
          },
      .flags.use_dma2d = 1,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(s_bus, &dpi, &s_panel), TAG, "DPI panel");

  panel_reset();
  for (size_t i = 0; i < BOARD_P4_PANEL_INIT_CMDS_SIZE; i++) {
    const board_p4_panel_init_cmd_t* e = &board_p4_panel_init_cmds[i];
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_dbi, e->cmd, e->data, e->len), TAG, "init 0x%02X", e->cmd);
    if (e->delay_ms) vTaskDelay(pdMS_TO_TICKS(e->delay_ms));
  }
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");

  // The DPI fetch shares PSRAM with Wi-Fi and the PPA: give it first claim so
  // a busy redraw doesn't starve the scan.
  axi_icm_ll_set_dw_gdma_qos_arbiter_prio(0, 0, 15);
  axi_icm_ll_set_dw_gdma_qos_arbiter_prio(1, 0, 15);

  void *fb0 = NULL, *fb1 = NULL;
  ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1), TAG, "framebuffers");
  s_fb[0] = (uint16_t*)fb0;
  s_fb[1] = (uint16_t*)fb1;
  const size_t bytes = (size_t)P4_LCD_H_RES * P4_LCD_V_RES * 2;
  for (int i = 0; i < 2; i++) {
    memset(s_fb[i], 0, bytes);
    esp_cache_msync(s_fb[i], bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }
  s_back = 1;  // the DPI starts on fb0

  ppa_client_config_t pc = {.oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1};
  ESP_RETURN_ON_ERROR(ppa_register_client(&pc, &s_ppa), TAG, "PPA client");
  return ESP_OK;
}

void p4_dsi_stop(void) {
  if (!s_panel) return;
  // A generated pattern stops the DPI reading PSRAM; deleting the driver keeps
  // its DMA quiet while OTA remaps flash.
  (void)esp_lcd_dpi_panel_set_pattern(s_panel, MIPI_DSI_PATTERN_BAR_VERTICAL);
  (void)esp_lcd_panel_del(s_panel);
  s_panel = NULL;
  s_fb[0] = s_fb[1] = NULL;
  if (s_ppa) {
    (void)ppa_unregister_client(s_ppa);
    s_ppa = NULL;
  }
}

void p4_dsi_present(const uint16_t* logical, int w, int h, bool upside_down) {
  if (!s_panel || !s_ppa || !logical) return;
  uint16_t* back = s_fb[s_back];
  ppa_srm_oper_config_t srm = {
      .in =
          {
              .buffer = logical,
              .pic_w = (uint32_t)w,
              .pic_h = (uint32_t)h,
              .block_w = (uint32_t)w,
              .block_h = (uint32_t)h,
              .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
          },
      .out =
          {
              .buffer = back,
              .buffer_size = (uint32_t)P4_LCD_H_RES * P4_LCD_V_RES * 2,
              .pic_w = P4_LCD_H_RES,
              .pic_h = P4_LCD_V_RES,
              .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
          },
      // 270 CCW is this module's landscape; 90 is the same mount turned over.
      .rotation_angle = upside_down ? PPA_SRM_ROTATION_ANGLE_90 : PPA_SRM_ROTATION_ANGLE_270,
      .scale_x = 1.0f,
      .scale_y = 1.0f,
      .mode = PPA_TRANS_MODE_BLOCKING,
  };
  esp_err_t e = ppa_do_scale_rotate_mirror(s_ppa, &srm);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "PPA rotate failed: %s", esp_err_to_name(e));
    return;
  }
  // One of the driver's own buffers: no copy, the DPI just scans it next.
  esp_lcd_panel_draw_bitmap(s_panel, 0, 0, P4_LCD_H_RES, P4_LCD_V_RES, back);
  s_back ^= 1;
}

#endif  // BOARD_JC4880P443
