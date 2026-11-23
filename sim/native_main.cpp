// Desktop renderer: draws the dashboard UI to PPM images for design review.
// Run via `pio run -e native && .pio/build/native/program sim/out`.
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include "../src/model.h"
#include "../src/sim_source.h"
#include "../src/ui/ui.h"

static const int W = 320, H = 240;
static lv_color_t fb[W * H];
static lv_color_t drawBuf[W * H];

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c) {
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) fb[y * W + x] = *c++;
  lv_disp_flush_ready(d);
}

static void settle() {
  for (int i = 0; i < 20; i++) {
    lv_tick_inc(20);
    lv_timer_handler();
  }
}

static void dump(const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    uint32_t c = lv_color_to32(fb[i]);
    unsigned char rgb[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

int main(int argc, char **argv) {
  std::string out = argc > 1 ? argv[1] : "sim/out";
  lv_init();
  static lv_disp_draw_buf_t db;
  lv_disp_draw_buf_init(&db, drawBuf, NULL, W * H);
  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.hor_res = W;
  drv.ver_res = H;
  drv.flush_cb = flush;
  drv.draw_buf = &db;
  lv_disp_drv_register(&drv);
  ui_init();

  const char *names[] = {"driving", "charging", "alarm", "offline", "realnow"};
  const char *pages[] = {"overview", "power", "alerts"};
  uint32_t now = 1000;
  for (int sc = 0; sc < 5; sc++) {
    Snapshot s;
    sim_scenario(s, sc);
    Alerts al;
    // Feed a plausible power history so the graph is populated.
    for (int k = 0; k < 150; k++) {
      Snapshot h = s;
      float wob = 1.0f + 0.5f * sinf(k / 7.0f) + 0.3f * sinf(k / 2.3f);
      for (int i = 0; i < 2; i++) if (h.b[i].valid) h.b[i].amps = s.b[i].amps * wob;
      alerts_eval(h, al, now);
      ui_update(h, al);
      now += 2000;
    }
    alerts_eval(s, al, now);
    ui_update(s, al);
    printf("scenario %-9s alerts=%d -> auto page after update: %d\n", names[sc], al.count, ui_page());
    settle();
    dump(out + "/" + names[sc] + "_auto.ppm");
    for (int p = 0; p < 3; p++) {
      ui_goto(p);
      ui_update(s, al);
      settle();
      dump(out + "/" + names[sc] + "_" + pages[p] + ".ppm");
    }
    if (al.count > 0) {
      // flash phase B, then silence the first alert and check it stops flashing
      ui_goto(PAGE_ALERTS);
      lv_tick_inc(500);
      lv_timer_handler();
      dump(out + "/" + names[sc] + "_alerts_flashB.ppm");
      ui_ack_row(0);
      lv_tick_inc(500);
      lv_timer_handler();
      dump(out + "/" + names[sc] + "_alerts_acked0.ppm");
      // staying on another page must hold: a repeat update with the same alerts must not yank us back
      ui_goto(PAGE_POWER);
      ui_update(s, al);
      printf("   after update with unchanged alerts, page stays: %d (expect 1)\n", ui_page());
    }
  }
  return 0;
}
