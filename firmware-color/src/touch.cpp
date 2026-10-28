// Touchscreen drivers for the CrowPanels, read by LVGL as a pointer device:
// GT911 (capacitive, I2C) on the 5" and 7", XPT2046 (resistive, SPI) on the
// 4.3", the AXS15231B's own touch (I2C) on the Guition 3.5". ui.cpp turns
// swipes, taps and long presses into navigation.
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

#elif TOUCH_AXS15231B
#include <Wire.h>

static bool axsFound = false;
// LVGL polls at this rate instead of the global 10 ms. A call inside the
// 20 ms read gap repeats the last point, and LVGL's gesture detection takes
// a zero move as the finger stopping and starts counting again, so up/down
// swipes often never reached the gesture limit.
#define TOUCH_READ_MS 25

#ifndef DEBUG_SWIPE
#define DEBUG_SWIPE 0
#endif

// One read: an 11-byte request, then gesture, point count and the first
// point (event in the top 2 bits of x high: 0 down, 1 lift, 2 contact; 12-bit
// x and y in portrait panel coordinates). Same rules as the SpoolmanScale
// port for this board:
//  - The chip never sends a clean "no finger". A lift is eight 0xFF bytes,
//    a frame of one repeated byte, a nonzero gesture byte, no points, or the
//    lift event; any of those releases at once.
//  - A read sooner than 20 ms after the last comes back malformed, so the bus
//    is read at most every 20 ms and calls in between repeat the last answer.
//  - The next tap reuses the same slot, moved. When a lift was missed, the
//    new finger still arrives as a touch-down event (0): that is a release
//    on this read and a new press on the next, so LVGL sees two touches
//    instead of a slide across whatever lies between them. Distance alone
//    can't tell: a quick page swipe moves 60+ px between reads.
//  - A failed I2C read keeps the last state.
static void readCb(lv_indev_drv_t*, lv_indev_data_t* d) {
  static lv_indev_data_t last = {};
  static uint32_t lastMs = 0;
  static bool arm = false;
  static lv_point_t next = {};
  static const uint8_t REQ[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
  *d = last;
  if (!axsFound) return;
  if (arm) {  // the release for a jump went out on the previous call
    arm = false;
    d->point = next;
    d->state = LV_INDEV_STATE_PR;
    last = *d;
    return;
  }
  uint32_t now = millis();
  if (now - lastMs < 20) return;
  lastMs = now;
  uint8_t b[8];
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(REQ, sizeof(REQ));
  if (Wire.endTransmission() != 0 || Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)8) != 8) return;
  for (uint8_t& v : b) v = Wire.read();
  bool same = true;
  for (uint8_t v : b) same = same && v == b[0];
  int rx = ((b[2] & 0x0F) << 8) | b[3], ry = ((b[4] & 0x0F) << 8) | b[5];
  bool lifted = same || b[0] != 0 || b[1] == 0 || (b[2] >> 6) == 1 || rx >= LCD_NATIVE_H || ry >= LCD_NATIVE_W;
  if (lifted) {
    d->state = LV_INDEV_STATE_REL;
  } else {
    lv_indev_data_t p = {};
    mapPoint(rx, ry, &p);
    if (last.state == LV_INDEV_STATE_PR && (b[2] >> 6) == 0) {
      arm = true;
      next = p.point;
      d->state = LV_INDEV_STATE_REL;
    } else {
      d->point = p.point;
      d->state = LV_INDEV_STATE_PR;
    }
  }
#if DEBUG_SWIPE
  if (d->state != last.state)
    Serial.printf("touch %s [%02X %02X %02X %02X %02X %02X]%s -> %d,%d\n", d->state == LV_INDEV_STATE_PR ? "down" : "up",
                  b[0], b[1], b[2], b[3], b[4], b[5], arm ? " jump" : "", d->point.x, d->point.y);
#endif
  last = *d;
}

static bool hwInit() {
  pinMode(TOUCH_INT, INPUT_PULLUP);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire.setTimeOut(10);  // a stuck read must not stall the UI loop
  Wire.beginTransmission(TOUCH_ADDR);
  axsFound = Wire.endTransmission() == 0;
  Serial.printf("AXS15231B touch %s (0x%02X)\n", axsFound ? "found" : "not found", TOUCH_ADDR);
  return axsFound;
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

// LVGL keeps a pressed object pressed wherever the finger goes and clicks it
// on release. A lift more than SLIDE_OFF_PX outside it cancels instead, as on
// a phone, and so does the end of a page drag that started on a button:
// wait_release turns the release into PRESS_LOST, which still ends the drag.
// Scrolls already send no click, and controls that follow the finger
// (slider, roller, keyboard) end as usual.
static const int SLIDE_OFF_PX = 24;

static void cancelIfSlidOff(const lv_point_t& p) {
  lv_indev_t* indev = lv_indev_get_act();
  if (!indev || indev->proc.wait_until_release) return;
  lv_obj_t* o = indev->proc.types.pointer.act_obj;
  if (!o || indev->proc.types.pointer.scroll_obj) return;
  if (ui_dragging()) {
    lv_indev_wait_release(indev);
    return;
  }
  if (lv_obj_check_type(o, &lv_slider_class) || lv_obj_check_type(o, &lv_roller_class) ||
      lv_obj_check_type(o, &lv_btnmatrix_class) || lv_obj_check_type(o, &lv_keyboard_class))
    return;
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  if (p.x >= a.x1 - SLIDE_OFF_PX && p.x <= a.x2 + SLIDE_OFF_PX && p.y >= a.y1 - SLIDE_OFF_PX &&
      p.y <= a.y2 + SLIDE_OFF_PX)
    return;
  lv_indev_wait_release(indev);
}

// A touch on a dimmed screen only wakes it: the whole press is reported as
// released, so it can't tap, swipe or open anything.
static void wakeFilter(lv_indev_drv_t* drv, lv_indev_data_t* d) {
  static bool swallow = false;
  static bool wasDown = false;
  readCb(drv, d);
  if (wasDown && d->state == LV_INDEV_STATE_REL && !swallow) cancelIfSlidOff(d->point);
  wasDown = d->state == LV_INDEV_STATE_PR;
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
  lv_indev_t* indev = lv_indev_drv_register(&indevDrv);
#ifdef TOUCH_READ_MS
  lv_timer_set_period(indev->driver->read_timer, TOUCH_READ_MS);
#else
  (void)indev;
#endif
}

#else
void touch_init() {}
#endif
