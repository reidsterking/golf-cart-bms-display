#include "sim_source.h"
#include <math.h>
#include <string.h>

static void fill(Battery &b, const char *serial, float soc, float amps, float tempC) {
  b.valid = true;
  strncpy(b.serial, serial, sizeof(b.serial) - 1);
  b.soc = soc;
  b.amps = amps;
  b.tempC = tempC;
  b.volts = 49.6f + soc * 0.033f - amps * 0.02f + (amps < 0 ? 3.0f : 0.0f);
  b.remainingAh = b.capacityAh * soc / 100.0f;
}

// Fake serials so a demo can never be mistaken for real modules.
static const char *SN1 = "DEMO00000001";
static const char *SN2 = "DEMO00000002";
static const char *MOD = "DEMO0000";

void sim_scenario(Snapshot &s, int id) {
  s.demo = true;
  s.linked = true;
  strncpy(s.module, MOD, sizeof(s.module) - 1);
  switch (id) {
    case 0:
      fill(s.b[0], SN1, 78, 14.2f, 27);
      fill(s.b[1], SN2, 74, 13.1f, 29);
      break;
    case 1:
      fill(s.b[0], SN1, 99, -8.0f, 26);
      fill(s.b[1], SN2, 99, -7.5f, 27);
      break;
    case 2:
      fill(s.b[0], SN1, 17, 45.0f, 47);
      fill(s.b[1], SN2, 9, 52.0f, 57);
      break;
    case 3:
      fill(s.b[0], SN1, 61, 6.0f, 25);
      s.b[1] = Battery();
      break;
    case 4:  // the real build today: module linked, no decoder yet
      s.demo = false;
      strncpy(s.module, "GC2RB48300087", sizeof(s.module) - 1);
      break;
    case 5:  // the real build with the module not found
      s.demo = false;
      s.linked = false;
      s.module[0] = 0;
      break;
  }
}

void sim_step(Snapshot &s, uint32_t nowMs) {
  static float soc = 92;
  static uint32_t last = 0;
  float t = nowMs / 1000.0f;
  float dt = (nowMs - last) / 1000.0f;
  last = nowMs;

  float total = 22 + 18 * sinf(t / 7) + 12 * sinf(t / 2.1f);
  if (total < -10) total = -10;
  float split[2] = {0.52f, 0.48f};

  soc -= (total * split[0]) * dt / 3600.0f / 30.0f * 100.0f * 40.0f;  // 40x time lapse
  if (soc < 6) soc = 95;

  s.demo = true;
  s.linked = true;
  strncpy(s.module, MOD, sizeof(s.module) - 1);
  for (int i = 0; i < 2; i++) {
    float amps = total * split[i];
    fill(s.b[i], i ? SN2 : SN1, soc - (i ? 1.5f : 0), amps, 24 + 0.10f * fabsf(amps) + 2 * i + 3 * sinf(t / 60));
  }
}
