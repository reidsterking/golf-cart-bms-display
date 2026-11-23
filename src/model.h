// Data model shared by the UI, the alert engine and the data sources
// (simulator now, Bluetooth decoder later). Pure data, no hardware calls.
#pragma once
#include <stdint.h>

struct Battery {
  bool valid = false;  // false when no recent data from this battery's module
  float volts = 0;
  float amps = 0;  // positive = discharging (cart is drawing), negative = charging
  float soc = 0;   // state of charge, percent
  float tempC = 0;
  float remainingAh = 0;
  float capacityAh = 30;  // placeholder until read from the module
};

struct Snapshot {
  Battery b[2];
};

enum Severity : uint8_t { SEV_INFO = 0, SEV_WARN = 1, SEV_CRIT = 2 };

struct Alert {
  Severity sev;
  char text[56];
};

struct Alerts {
  Alert items[10];
  int count = 0;
};

// Evaluates the snapshot and fills `out` with the alerts worth showing.
// Warnings must persist before they appear and stay a while before clearing,
// so the dashboard does not flicker or nag.
void alerts_eval(const Snapshot &s, Alerts &out, uint32_t nowMs);
