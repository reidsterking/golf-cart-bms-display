// Golf cart battery dashboard for the ESP32 Cheap Yellow Display (ESP32-2432S028R).
// Default build reads the real batteries over Bluetooth. Build env "cyd-demo" uses fake data.
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include "model.h"
#include "ble_source.h"
#include "sim_source.h"
#include "ui/ui.h"

// Panel quirks. If colors look inverted, set CYD_INVERT to 1.
#ifndef CYD_INVERT
#define CYD_INVERT 0
#endif
// Touch calibration (raw ADC range of the XPT2046 mapped to the screen).
static const int TOUCH_X_MIN = 200, TOUCH_X_MAX = 3700;
static const int TOUCH_Y_MIN = 240, TOUCH_Y_MAX = 3800;

static const int W = 320, H = 240;
static TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen touch(33, 36);  // CS, IRQ
static lv_disp_draw_buf_t drawBuf;
static lv_color_t buf[W * 20];

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushColors((uint16_t *)&c->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(d);
}

static void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
  if (touch.tirqTouched() && touch.touched()) {
    TS_Point p = touch.getPoint();
    int x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, W);
    int y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, H);
    data->point.x = constrain(x, 0, W - 1);
    data->point.y = constrain(y, 0, H - 1);
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);  // landscape, USB connector on the left
  tft.invertDisplay(CYD_INVERT);
  tft.fillScreen(TFT_BLACK);

  touchSpi.begin(25, 39, 32, 33);  // SCLK, MISO, MOSI, CS
  touch.begin(touchSpi);
  touch.setRotation(1);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf, NULL, W * 20);
  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = W;
  dispDrv.ver_res = H;
  dispDrv.flush_cb = flush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = readTouch;
  lv_indev_drv_register(&indevDrv);

  ui_init();
#ifndef DEMO_DATA
  ble_start();
#endif
}

void loop() {
  static uint32_t lastData = 0;
  uint32_t now = millis();
  if (now - lastData >= 2000) {
    lastData = now;
    static Snapshot snap;
    static Alerts alerts;
#ifdef DEMO_DATA
    sim_step(snap, now);  // fake numbers, labeled DEMO DATA on screen
#else
    ble_snapshot(snap);   // real batteries only
#endif
    alerts_eval(snap, alerts, now);
    ui_update(snap, alerts);
  }
  lv_timer_handler();
  delay(5);
}
