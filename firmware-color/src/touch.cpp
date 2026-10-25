// Touchscreen drivers for the CrowPanels, read by LVGL as a pointer device:
// GT911 (capacitive, I2C) on the 5" and 7", XPT2046 (resistive, SPI) on the
// 4.3". ui.cpp turns swipes, taps and long presses into navigation.
#include "touch.h"

#include <Arduino.h>
#include <lvgl.h>

#include "board.h"
#include "ui.h"

#if INPUT_TOUCH

static lv_indev_drv_t indevDrv;

static void mapPoint(int rx, int ry, lv_indev_data_t* d) {
  int x = TOUCH_SWAP_XY ? ry : rx, y = TOUCH_SWAP_XY ? rx : ry;
  if (TOUCH_FLIP_X) x = LCD_NATIVE_W - 1 - x;
  if (TOUCH_FLIP_Y) y = LCD_NATIVE_H - 1 - y;
  d->point.x = constrain(x, 0, LCD_NATIVE_W - 1);
  d->point.y = constrain(y, 0, LCD_NATIVE_H - 1);
}

#if TOUCH_GT911
#include <Wire.h>

#include "ch422g.h"

static uint8_t gtAddr = 0;

static bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static bool gtRead(uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(gtAddr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void gtWrite(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(val);
  Wire.endTransmission();
}

#ifdef TOUCH_PCA9557
// Board v3 keeps the GT911 in reset via the PCA9557 (same sequence as
// Elecrow's demo and the Digital Swatch firmware).
static void releaseViaPca9557() {
  const uint8_t pca = TOUCH_PCA9557;
  if (!i2cProbe(pca)) return;
  i2cWriteReg(pca, 0x03, 0xFF);
  i2cWriteReg(pca, 0x01, 0xFF);
  i2cWriteReg(pca, 0x02, 0x00);
  i2cWriteReg(pca, 0x03, 0x00);
  i2cWriteReg(pca, 0x01, 0xFC);
  delay(20);
  i2cWriteReg(pca, 0x01, 0xFD);
  delay(100);
  i2cWriteReg(pca, 0x03, 0x02);
  delay(50);
}
#endif

static bool findGt911() {
  for (uint8_t a : {0x5D, 0x14}) {
    if (i2cProbe(a)) {
      gtAddr = a;
      return true;
    }
  }
  return false;
}

#ifndef DEBUG_SWIPE
#define DEBUG_SWIPE 0
#endif

static void readCb(lv_indev_drv_t*, lv_indev_data_t* d) {
  static lv_indev_data_t last = {};
#if DEBUG_SWIPE
  static uint32_t lastLog = 0;
  static int lastState = -1;
  struct Logger {
    lv_indev_data_t* d;
    ~Logger() {
      if ((int)d->state != lastState) {
        Serial.printf("touch %s %d,%d at %lu (+%lu)\n", d->state == LV_INDEV_STATE_PR ? "down" : "up", d->point.x,
                      d->point.y, (unsigned long)millis(), (unsigned long)(millis() - lastLog));
        lastState = d->state;
        lastLog = millis();
      }
    }
  } logger{d};
#endif
  d->point = last.point;
  d->state = LV_INDEV_STATE_REL;
  uint8_t status;
  if (!gtAddr || !gtRead(0x814E, &status, 1)) return;
  if (!(status & 0x80)) {
    d->state = last.state;  // no new report yet
    return;
  }
  uint8_t pt[4];
  if ((status & 0x0F) && gtRead(0x8150, pt, 4)) {
    mapPoint(pt[0] | (pt[1] << 8), pt[2] | (pt[3] << 8), d);
    d->state = LV_INDEV_STATE_PR;
  }
  gtWrite(0x814E, 0);
  last = *d;
}

static bool hwInit() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
#if LCD_CH422G
  // Reset through the expander with INT low: the GT911 comes up at 0x5D.
  pinMode(TOUCH_INT, OUTPUT);
  digitalWrite(TOUCH_INT, LOW);
  ch422g_write(CH422G_TOUCH_RST, false);
  delay(10);
  ch422g_write(CH422G_TOUCH_RST, true);
  delay(10);
  pinMode(TOUCH_INT, INPUT);
  delay(60);
  findGt911();
#else
  if (!findGt911()) {
    releaseViaPca9557();
    findGt911();
  }
#endif
  Serial.printf("GT911 %s (0x%02X)\n", gtAddr ? "found" : "not found", gtAddr);
  return gtAddr != 0;
}

#elif TOUCH_XPT2046
#include <SPI.h>

static SPIClass touchSpi(FSPI);
static const SPISettings XPT_SPI(1000000, MSBFIRST, SPI_MODE0);

static uint16_t xptRead(uint8_t cmd) {
  touchSpi.transfer(cmd);
  return touchSpi.transfer16(0) >> 3;
}

static void readCb(lv_indev_drv_t*, lv_indev_data_t* d) {
  static lv_point_t last = {};
  d->point = last;
  d->state = LV_INDEV_STATE_REL;
  if (digitalRead(TOUCH_INT) != LOW) return;  // INT is low while pressed
  long sx = 0, sy = 0;
  touchSpi.beginTransaction(XPT_SPI);
  digitalWrite(TOUCH_CS, LOW);
  xptRead(0xD1);  // first sample after waking is noisy
  for (int i = 0; i < 4; i++) {
    sx += xptRead(0xD1);
    sy += xptRead(0x91);
  }
  xptRead(0x90);  // back to power-down with the pen IRQ enabled
  digitalWrite(TOUCH_CS, HIGH);
  touchSpi.endTransaction();
  int rx = map(sx / 4, TOUCH_RAW_X0, TOUCH_RAW_X1, 0, LCD_NATIVE_W - 1);
  int ry = map(sy / 4, TOUCH_RAW_Y0, TOUCH_RAW_Y1, 0, LCD_NATIVE_H - 1);
  mapPoint(rx, ry, d);
  d->state = LV_INDEV_STATE_PR;
  last = d->point;
}

static bool hwInit() {
  pinMode(TOUCH_INT, INPUT_PULLUP);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  touchSpi.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  return true;
}
#endif

// A touch on a dimmed screen only wakes it: the whole press is reported as
// released, so it can't tap, swipe or open anything.
static void wakeFilter(lv_indev_drv_t* drv, lv_indev_data_t* d) {
  static bool swallow = false;
  readCb(drv, d);
  if (d->state == LV_INDEV_STATE_PR) {
    if (!swallow && ui_wake()) swallow = true;
  } else {
    swallow = false;
  }
  if (swallow) d->state = LV_INDEV_STATE_REL;
}

void touch_init() {
  hwInit();
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = wakeFilter;
  // A finger moves a bit while tapping; keep swipes deliberate.
  indevDrv.gesture_limit = 60;
  indevDrv.gesture_min_velocity = 3;
  lv_indev_drv_register(&indevDrv);
}

#else
void touch_init() {}
#endif
