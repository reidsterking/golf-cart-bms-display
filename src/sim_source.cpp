#include "sim_source.h"
#include <math.h>

static void fill(Battery &b, float soc, float amps, float tempC) {
  b.valid = true;
  b.soc = soc;
  b.amps = amps;
  b.tempC = tempC;
  b.volts = 49.6f + soc * 0.033f - amps * 0.02f + (amps < 0 ? 3.0f : 0.0f);
  b.remainingAh = b.capacityAh * soc / 100.0f;
}

void sim_scenario(Snapshot &s, int id) {
  switch (id) {
    case 0:
      fill(s.b[0], 78, 14.2f, 27);
      fill(s.b[1], 74, 13.1f, 29);
      break;
    case 1:
      fill(s.b[0], 99, -8.0f, 26);
      fill(s.b[1], 99, -7.5f, 27);
      break;
    case 2:
      fill(s.b[0], 17, 45.0f, 47);
      fill(s.b[1], 9, 52.0f, 57);
      break;
    case 3:
      fill(s.b[0], 61, 6.0f, 25);
      s.b[1] = Battery();
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

  for (int i = 0; i < 2; i++) {
    float amps = total * split[i];
    fill(s.b[i], soc - (i ? 1.5f : 0), amps, 24 + 0.10f * fabsf(amps) + 2 * i + 3 * sinf(t / 60));
  }
}
