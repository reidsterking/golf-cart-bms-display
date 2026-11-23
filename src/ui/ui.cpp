#include "ui.h"
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DEG "\xC2\xB0"

namespace {

// Palette
const uint32_t BG = 0x0B0F14, CARD = 0x151C24, LINE = 0x26323F, TXT = 0xE6EDF3, MUTED = 0x7D8B99;
const uint32_t ACCENT = 0x2DD4BF, GOOD = 0x34D399, WARN = 0xFBBF24, CRIT = 0xF87171, CHG = 0x60A5FA;
const uint32_t TINT_CRIT = 0x3A1A1D, TINT_WARN = 0x33290F, TINT_INFO = 0x0F2F33;

const int W = 320, H = 240;
const int BAR_H = 28, TAB_H = 30, PAGE_Y = BAR_H, PAGE_H = H - BAR_H - TAB_H;
const int HIST = 150;
const int MAX_ALERT_ROWS = 6;
const int MAX_IDS = 12;

const lv_font_t *F_XS = &lv_font_montserrat_12;
const lv_font_t *F_SM = &lv_font_montserrat_14;
const lv_font_t *F_MD = &lv_font_montserrat_16;
const lv_font_t *F_LG = &lv_font_montserrat_20;
const lv_font_t *F_XL = &lv_font_montserrat_28;

inline lv_color_t C(uint32_t h) { return lv_color_hex(h); }

struct Card {
  lv_obj_t *title, *dot, *arc, *soc, *volts, *amps, *temp;
};
Card cards[2];
lv_obj_t *pages[3], *tabLabel[3], *tabLine[3];
lv_obj_t *titleLbl, *bleIcon[2], *chip, *chipLabel;
lv_obj_t *sysCaption, *sysAmps, *sysPower, *sysEnergy;
lv_obj_t *chart, *pwrBig, *pwrSub;
lv_chart_series_t *series;
lv_obj_t *alertRow[MAX_ALERT_ROWS], *alertStripe[MAX_ALERT_ROWS], *alertText[MAX_ALERT_ROWS];
lv_obj_t *alertEmpty;
int16_t hist[HIST];
int histCount = 0;
int currentPage = 0;

// Alert state
struct Shown {
  uint8_t id;
  int8_t sev;
};
Shown shown[MAX_ALERT_ROWS];
int shownCount = 0;
int8_t prevSev[MAX_IDS];  // severity last seen per alert id, -1 = not active
int8_t ackSev[MAX_IDS];   // severity the user silenced, -1 = not silenced
bool flashOn = false;
bool demoMode = false;

uint32_t sevColor(int sev) { return sev >= SEV_CRIT ? CRIT : (sev == SEV_WARN ? WARN : ACCENT); }
uint32_t sevTint(int sev) { return sev >= SEV_CRIT ? TINT_CRIT : (sev == SEV_WARN ? TINT_WARN : TINT_INFO); }

lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, uint32_t bg, int radius) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, C(bg), 0);
  lv_obj_set_style_radius(o, radius, 0);
  return o;
}

lv_obj_t *lbl(lv_obj_t *p, int x, int y, const lv_font_t *f, uint32_t col, const char *t) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, C(col), 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

void setColor(lv_obj_t *l, uint32_t col) { lv_obj_set_style_text_color(l, C(col), 0); }

uint32_t socColor(float soc) { return soc < 20 ? CRIT : (soc < 40 ? WARN : GOOD); }
uint32_t tempColor(float c) { return c > 55 ? CRIT : (c > 45 ? WARN : TXT); }

bool unacked(int row) { return shown[row].sev > ackSev[shown[row].id]; }

// Drives everything that flashes or depends on acknowledgement state.
void applyFlash() {
  bool anyUnacked = false;
  int worstUnacked = -1, worstAll = -1;
  for (int r = 0; r < shownCount; r++) {
    if (shown[r].sev > worstAll) worstAll = shown[r].sev;
    bool un = unacked(r);
    if (un) {
      anyUnacked = true;
      if (shown[r].sev > worstUnacked) worstUnacked = shown[r].sev;
    }
    lv_obj_set_style_bg_color(alertRow[r], C(un && flashOn ? sevTint(shown[r].sev) : CARD), 0);
  }

  // Status pill: flashes while anything is unacknowledged, steady tint otherwise
  uint32_t chipBg = CARD;
  if (anyUnacked) chipBg = flashOn ? sevTint(worstUnacked) : CARD;
  else if (worstAll >= 0) chipBg = sevTint(worstAll);
  lv_obj_set_style_bg_color(chip, C(chipBg), 0);

  // Title doubles as a hint while alerts are flashing on the alerts page
  if (anyUnacked && currentPage == PAGE_ALERTS) {
    lv_label_set_text(titleLbl, "TAP TO MUTE");
    setColor(titleLbl, TXT);
  } else if (demoMode) {
    lv_label_set_text(titleLbl, "DEMO DATA");
    setColor(titleLbl, WARN);
  } else {
    lv_label_set_text(titleLbl, "EPOCH 48V");
    setColor(titleLbl, MUTED);
  }

  // Tab labels. The alerts tab blinks when there are unacknowledged alerts elsewhere.
  for (int i = 0; i < 3; i++) setColor(tabLabel[i], i == currentPage ? TXT : MUTED);
  if (anyUnacked && currentPage != PAGE_ALERTS) setColor(tabLabel[PAGE_ALERTS], flashOn ? sevColor(worstUnacked) : TXT);
}

void showPage(int p) {
  currentPage = p;
  for (int i = 0; i < 3; i++) {
    if (i == p) lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(tabLine[i], i == p ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
  applyFlash();
}

void onTab(lv_event_t *e) { showPage((int)(intptr_t)lv_event_get_user_data(e)); }
void onAlertTap(lv_event_t *e) { ui_ack_row((int)(intptr_t)lv_event_get_user_data(e)); }
void onFlashTimer(lv_timer_t *) {
  flashOn = !flashOn;
  applyFlash();
}

void buildStatusBar(lv_obj_t *scr) {
  titleLbl = lbl(scr, 10, 7, F_SM, MUTED, "EPOCH 48V");
  chip = box(scr, 120, 4, 100, 20, CARD, 10);
  chipLabel = lbl(chip, 0, 0, F_XS, GOOD, "ALL CLEAR");
  lv_obj_center(chipLabel);
  for (int i = 0; i < 2; i++) {
    bleIcon[i] = lbl(scr, 236 + i * 40, 6, F_SM, MUTED, LV_SYMBOL_BLUETOOTH);
    char n[4];
    snprintf(n, sizeof n, "%d", i + 1);
    lbl(scr, 252 + i * 40, 7, F_XS, MUTED, n);
  }
}

void buildCard(lv_obj_t *page, int i) {
  Card &c = cards[i];
  int x = 6 + i * 157;
  lv_obj_t *card = box(page, x, 4, 151, 112, CARD, 10);
  char t[24];
  snprintf(t, sizeof t, "BATTERY %d", i + 1);
  c.title = lbl(card, 10, 6, F_XS, MUTED, t);
  c.dot = box(card, 133, 9, 8, 8, MUTED, 4);

  c.arc = lv_arc_create(card);
  lv_obj_set_size(c.arc, 78, 78);
  lv_obj_set_pos(c.arc, 6, 26);
  lv_arc_set_rotation(c.arc, 135);
  lv_arc_set_bg_angles(c.arc, 0, 270);
  lv_arc_set_range(c.arc, 0, 100);
  lv_arc_set_value(c.arc, 0);
  lv_obj_remove_style(c.arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(c.arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(c.arc, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_width(c.arc, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(c.arc, C(LINE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(c.arc, C(GOOD), LV_PART_INDICATOR);

  c.soc = lbl(c.arc, 0, 0, F_LG, TXT, "--");
  lv_obj_center(c.soc);

  c.volts = lbl(card, 94, 30, F_MD, TXT, "--");
  c.amps = lbl(card, 94, 56, F_MD, TXT, "--");
  c.temp = lbl(card, 94, 82, F_MD, TXT, "--");
}

void buildOverview(lv_obj_t *page) {
  buildCard(page, 0);
  buildCard(page, 1);

  lv_obj_t *strip = box(page, 6, 122, 308, 58, CARD, 10);
  sysCaption = lbl(strip, 12, 6, F_XS, MUTED, "DRAWING");
  sysAmps = lbl(strip, 12, 22, F_XL, TXT, "--");
  lbl(strip, 116, 6, F_XS, MUTED, "POWER");
  sysPower = lbl(strip, 116, 27, F_LG, TXT, "--");
  lbl(strip, 206, 6, F_XS, MUTED, "REMAINING");
  sysEnergy = lbl(strip, 206, 27, F_LG, TXT, "--");
}

void buildPower(lv_obj_t *page) {
  pwrBig = lbl(page, 12, 6, F_XL, TXT, "--");
  pwrSub = lbl(page, 12, 40, F_XS, MUTED, "");
  lv_obj_t *cap = lbl(page, 0, 0, F_XS, MUTED, "LAST 5 MIN");
  lv_obj_align(cap, LV_ALIGN_TOP_RIGHT, -12, 10);

  chart = lv_chart_create(page);
  lv_obj_set_pos(chart, 6, 58);
  lv_obj_set_size(chart, 308, 120);
  lv_obj_set_style_bg_color(chart, C(CARD), 0);
  lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_radius(chart, 10, 0);
  lv_obj_set_style_pad_all(chart, 8, 0);
  lv_obj_set_style_line_color(chart, C(LINE), LV_PART_MAIN);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, HIST);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -20, 60);  // tenths of kW
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(chart, 4, 0);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
  series = lv_chart_add_series(chart, C(ACCENT), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_value(chart, series, LV_CHART_POINT_NONE);
  // Scale labels: the chart spans -2..6 kW, so zero sits three quarters of the way down.
  lbl(page, 12, 62, F_XS, MUTED, "6 kW");
  lbl(page, 12, 130, F_XS, MUTED, "0");
}

void buildAlerts(lv_obj_t *page) {
  for (int i = 0; i < MAX_ALERT_ROWS; i++) {
    alertRow[i] = box(page, 6, 4 + i * 30, 308, 27, CARD, 8);
    lv_obj_add_flag(alertRow[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(alertRow[i], onAlertTap, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    alertStripe[i] = box(alertRow[i], 0, 0, 5, 27, WARN, 2);
    alertText[i] = lbl(alertRow[i], 16, 5, F_SM, TXT, "");
    lv_obj_add_flag(alertRow[i], LV_OBJ_FLAG_HIDDEN);
  }
  alertEmpty = lbl(page, 0, 0, F_MD, GOOD, LV_SYMBOL_OK "  All systems normal");
  lv_obj_align(alertEmpty, LV_ALIGN_CENTER, 0, -8);
}

void buildTabs(lv_obj_t *scr) {
  const char *names[3] = {"OVERVIEW", "POWER", "ALERTS"};
  int tw = W / 3;
  box(scr, 0, H - TAB_H, W, TAB_H, CARD, 0);
  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = box(scr, i * tw, H - TAB_H, tw, TAB_H, CARD, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, onTab, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    tabLabel[i] = lbl(b, 0, 0, F_SM, MUTED, names[i]);
    lv_obj_center(tabLabel[i]);
    tabLine[i] = box(b, 14, 0, tw - 28, 3, ACCENT, 1);
    lv_obj_set_style_bg_opa(tabLine[i], LV_OPA_TRANSP, 0);
  }
}

void last4(const char *serial, char *out, size_t n) {
  size_t len = strlen(serial);
  snprintf(out, n, "%s", len > 4 ? serial + len - 4 : serial);
}

}  // namespace

void ui_init() {
  for (int i = 0; i < MAX_IDS; i++) prevSev[i] = ackSev[i] = -1;
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C(BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildStatusBar(scr);
  for (int i = 0; i < 3; i++) pages[i] = box(scr, 0, PAGE_Y, W, PAGE_H, BG, 0);
  buildOverview(pages[0]);
  buildPower(pages[1]);
  buildAlerts(pages[2]);
  buildTabs(scr);
  showPage(PAGE_OVERVIEW);
  lv_timer_create(onFlashTimer, 500, NULL);
}

void ui_goto(int page) { showPage(page); }
int ui_page() { return currentPage; }

void ui_ack_row(int row) {
  if (row < 0 || row >= shownCount) return;
  ackSev[shown[row].id] = shown[row].sev;
  applyFlash();
}

void ui_update(const Snapshot &s, const Alerts &al) {
  float totA = 0, totW = 0, totWh = 0;
  int validN = 0, onlyIdx = 0;
  char buf[48];
  demoMode = s.demo;

  for (int i = 0; i < 2; i++) {
    const Battery &b = s.b[i];
    Card &c = cards[i];

    if (b.serial[0]) {
      char tail[8];
      last4(b.serial, tail, sizeof tail);
      snprintf(buf, sizeof buf, "BATTERY %d   #%s", i + 1, tail);
    } else {
      snprintf(buf, sizeof buf, "BATTERY %d", i + 1);
    }
    lv_label_set_text(c.title, buf);
    // Dot: green = live readings, amber = linked but no readings, grey = not connected
    lv_obj_set_style_bg_color(c.dot, C(b.valid ? GOOD : (b.linked ? WARN : MUTED)), 0);
    setColor(bleIcon[i], b.linked ? ACCENT : MUTED);

    if (!b.valid) {
      lv_arc_set_value(c.arc, 0);
      lv_obj_set_style_arc_color(c.arc, C(LINE), LV_PART_INDICATOR);
      lv_label_set_text(c.soc, "--");
      lv_label_set_text(c.volts, "--");
      lv_label_set_text(c.amps, "--");
      lv_label_set_text(c.temp, "--");
      setColor(c.amps, MUTED);
      setColor(c.temp, MUTED);
      continue;
    }
    lv_arc_set_value(c.arc, (int)lroundf(b.soc));
    lv_obj_set_style_arc_color(c.arc, C(socColor(b.soc)), LV_PART_INDICATOR);
    lv_label_set_text_fmt(c.soc, "%d%%", (int)lroundf(b.soc));
    snprintf(buf, sizeof buf, "%.1f V", b.volts);
    lv_label_set_text(c.volts, buf);
    snprintf(buf, sizeof buf, "%s%.1f A", b.amps < -0.5f ? LV_SYMBOL_CHARGE " " : "", fabsf(b.amps));
    lv_label_set_text(c.amps, buf);
    setColor(c.amps, b.amps < -0.5f ? CHG : TXT);
    snprintf(buf, sizeof buf, "%.0f" DEG "F", cToF(b.tempC));
    lv_label_set_text(c.temp, buf);
    setColor(c.temp, tempColor(b.tempC));
    validN++;
    onlyIdx = i;
    totA += b.amps;
    totW += b.volts * b.amps;
    totWh += b.remainingAh * b.volts;
  }

  // System totals. With no readings they show "--", never zeros that look measured.
  if (validN == 0) {
    lv_label_set_text(sysCaption, "NO DATA");
    setColor(sysCaption, MUTED);
    lv_label_set_text(sysAmps, "--");
    setColor(sysAmps, MUTED);
    lv_label_set_text(sysPower, "--");
    lv_label_set_text(sysEnergy, "--");
  } else {
    if (validN == 1) snprintf(buf, sizeof buf, "BAT %d ONLY", onlyIdx + 1);
    else snprintf(buf, sizeof buf, "%s", totA > 1 ? "DRAWING" : (totA < -1 ? "CHARGING" : "IDLE"));
    lv_label_set_text(sysCaption, buf);
    setColor(sysCaption, (validN == 2 && totA < -1) ? CHG : MUTED);
    snprintf(buf, sizeof buf, "%.1f A", fabsf(totA));
    lv_label_set_text(sysAmps, buf);
    setColor(sysAmps, totA < -1 ? CHG : TXT);
    snprintf(buf, sizeof buf, "%.2f kW", fabsf(totW) / 1000.0f);
    lv_label_set_text(sysPower, buf);
    snprintf(buf, sizeof buf, "%.2f kWh", totWh / 1000.0f);
    lv_label_set_text(sysEnergy, buf);
  }

  // Power history (gaps when there is no data)
  const int16_t NONE = -32768;
  int16_t sample = validN ? (int16_t)lroundf(totW / 100.0f) : NONE;  // tenths of kW
  lv_chart_set_next_value(chart, series, validN ? sample : LV_CHART_POINT_NONE);
  if (histCount < HIST) hist[histCount++] = sample;
  else {
    for (int i = 1; i < HIST; i++) hist[i - 1] = hist[i];
    hist[HIST - 1] = sample;
  }
  int peak = 0;
  for (int i = 0; i < histCount; i++) if (hist[i] != NONE && hist[i] > peak) peak = hist[i];
  if (validN == 0) {
    lv_label_set_text(pwrBig, "--");
    setColor(pwrBig, MUTED);
    lv_label_set_text(pwrSub, "no readings from the batteries yet");
  } else {
    snprintf(buf, sizeof buf, "%.2f kW", totW / 1000.0f);
    lv_label_set_text(pwrBig, buf);
    setColor(pwrBig, totW < -50 ? CHG : TXT);
    snprintf(buf, sizeof buf, "%.1f A total    peak %.1f kW", totA, peak / 10.0f);
    lv_label_set_text(pwrSub, buf);
  }

  // Alerts: detect new or worsened alerts, jump to the alerts page for them
  int8_t nowSev[MAX_IDS];
  for (int i = 0; i < MAX_IDS; i++) nowSev[i] = -1;
  for (int i = 0; i < al.count; i++) if (al.items[i].id < MAX_IDS) nowSev[al.items[i].id] = al.items[i].sev;
  bool fresh = false;
  for (int i = 0; i < MAX_IDS; i++) {
    if (nowSev[i] > prevSev[i]) {
      fresh = true;
      ackSev[i] = -1;  // a new or worse alert always flashes again
    }
    if (nowSev[i] < 0) ackSev[i] = -1;
    prevSev[i] = nowSev[i];
  }

  shownCount = al.count < MAX_ALERT_ROWS ? al.count : MAX_ALERT_ROWS;
  int worst = -1;
  for (int i = 0; i < al.count; i++) if ((int)al.items[i].sev > worst) worst = al.items[i].sev;
  for (int i = 0; i < MAX_ALERT_ROWS; i++) {
    if (i < shownCount) {
      const Alert &a = al.items[i];
      shown[i].id = a.id < MAX_IDS ? a.id : 0;
      shown[i].sev = a.sev;
      lv_obj_set_style_bg_color(alertStripe[i], C(sevColor(a.sev)), 0);
      lv_label_set_text(alertText[i], a.text);
      lv_obj_clear_flag(alertRow[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(alertRow[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (al.count == 0) {
    lv_label_set_text(chipLabel, "ALL CLEAR");
    setColor(chipLabel, GOOD);
    lv_label_set_text(tabLabel[PAGE_ALERTS], "ALERTS");
    lv_obj_clear_flag(alertEmpty, LV_OBJ_FLAG_HIDDEN);
  } else {
    snprintf(buf, sizeof buf, "%d %s", al.count, al.count == 1 ? "ALERT" : "ALERTS");
    lv_label_set_text(chipLabel, buf);
    setColor(chipLabel, sevColor(worst));
    snprintf(buf, sizeof buf, "ALERTS (%d)", al.count);
    lv_label_set_text(tabLabel[PAGE_ALERTS], buf);
    lv_obj_add_flag(alertEmpty, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_center(chipLabel);
  lv_obj_center(tabLabel[PAGE_ALERTS]);

  if (fresh) showPage(PAGE_ALERTS);
  else applyFlash();
}
