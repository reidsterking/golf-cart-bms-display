// Data model shared by the UI, the alert engine and the data sources
// (live Bluetooth or the demo simulator). Pure data, no hardware calls.
// All temperatures are Celsius here; the UI converts to Fahrenheit for display.
#pragma once
#include <stdint.h>

struct Battery {
  bool linked = false;  // Bluetooth connection to this battery's module is up
  bool valid = false;   // fresh, decoded readings are available
  char serial[16] = {0};  // module name, e.g. "GC2RB48300087"
  float volts = 0;
  float amps = 0;  // positive = discharging (cart is drawing), negative = charging
  float soc = 0;   // state of charge, percent
  float tempC = 0;
  float remainingAh = 0;
  float capacityAh = 30;  // placeholder until read from the module
};

struct Snapshot {
  Battery b[2];
  bool demo = false;  // true only for the simulator build
};

enum Severity : uint8_t { SEV_INFO = 0, SEV_WARN = 1, SEV_CRIT = 2 };

struct Alert {
  uint8_t id;  // stable per condition and battery, so the UI can track it
  Severity sev;
  char text[56];
};

struct Alerts {
  Alert items[10];
  int count = 0;
};

inline float cToF(float c) { return c * 9.0f / 5.0f + 32.0f; }

// Evaluates the snapshot and fills `out` with the alerts worth showing.
// Warnings must persist before they appear and stay a while before clearing,
// so the dashboard does not flicker or nag.
void alerts_eval(const Snapshot &s, Alerts &out, uint32_t nowMs);
