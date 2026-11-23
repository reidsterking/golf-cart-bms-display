#include "model.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Thresholds are placeholders until we read the real limits from the batteries.
namespace {
const float SOC_WARN = 20, SOC_CRIT = 10;
const float TEMP_WARN_C = 45, TEMP_CRIT_C = 55;
const float CURRENT_WARN = 100;
const uint32_t SHOW_AFTER_MS = 5000;   // condition must hold this long to appear
const uint32_t CLEAR_AFTER_MS = 10000;  // and be gone this long to disappear

enum Metric { M_SOC, M_HOT, M_COLD_CHARGE, M_CURRENT, M_FULL, M_NODATA, M_COUNT };

// Levels: 0 none, 1 info, 2 warn, 3 critical
struct Slot {
  uint8_t shown = 0;
  uint32_t since = 0;
  bool changing = false;
};
Slot slots[M_COUNT][2];

uint8_t candidate(int m, const Battery &b) {
  if (m == M_NODATA) return b.valid ? 0 : 2;
  if (!b.valid) return 0;
  switch (m) {
    case M_SOC: return b.soc < SOC_CRIT ? 3 : (b.soc < SOC_WARN ? 2 : 0);
    case M_HOT: return b.tempC > TEMP_CRIT_C ? 3 : (b.tempC > TEMP_WARN_C ? 2 : 0);
    case M_COLD_CHARGE: return (b.tempC < 0 && b.amps < -1) ? 3 : 0;
    case M_CURRENT: return fabsf(b.amps) > CURRENT_WARN ? 2 : 0;
    case M_FULL: return (b.soc >= 99 && b.amps < -0.5f) ? 1 : 0;
  }
  return 0;
}

void describe(int m, int i, uint8_t level, const Battery &b, char *out, size_t n) {
  int id = i + 1;
  switch (m) {
    case M_SOC:
      snprintf(out, n, level == 3 ? "BAT %d: Critically low, %.0f%%" : "BAT %d: Low charge, %.0f%%", id, b.soc);
      break;
    case M_HOT:
      snprintf(out, n, level == 3 ? "BAT %d: Overheating, %.0f\xC2\xB0" "F" : "BAT %d: Running hot, %.0f\xC2\xB0" "F",
               id, cToF(b.tempC));
      break;
    case M_COLD_CHARGE: snprintf(out, n, "BAT %d: Charging below freezing", id); break;
    case M_CURRENT: snprintf(out, n, "BAT %d: High current, %.0f A", id, fabsf(b.amps)); break;
    case M_FULL: snprintf(out, n, "BAT %d: Full, unplug the charger", id); break;
    case M_NODATA:
      snprintf(out, n, b.linked ? "BAT %d: Linked, no readings yet" : "BAT %d: Module not found", id);
      break;
  }
}
}  // namespace

void alerts_eval(const Snapshot &s, Alerts &out, uint32_t nowMs) {
  out.count = 0;
  for (int m = 0; m < M_COUNT; m++) {
    for (int i = 0; i < 2; i++) {
      Slot &sl = slots[m][i];
      uint8_t cand = candidate(m, s.b[i]);
      if (cand != sl.shown) {
        if (!sl.changing) {
          sl.changing = true;
          sl.since = nowMs;
        }
        uint32_t need = cand > sl.shown ? SHOW_AFTER_MS : CLEAR_AFTER_MS;
        if (nowMs - sl.since >= need) {
          sl.shown = cand;
          sl.changing = false;
        }
      } else {
        sl.changing = false;
      }
    }
  }
  // Most severe first
  for (int level = 3; level >= 1; level--) {
    for (int m = 0; m < M_COUNT; m++) {
      for (int i = 0; i < 2; i++) {
        if (slots[m][i].shown != level || out.count >= 10) continue;
        Alert &a = out.items[out.count++];
        a.id = (uint8_t)(m * 2 + i);
        a.sev = (Severity)(level - 1);
        describe(m, i, level, s.b[i], a.text, sizeof(a.text));
      }
    }
  }
}
