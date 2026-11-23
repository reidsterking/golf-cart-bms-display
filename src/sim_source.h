// Simulated battery data, used until the Bluetooth decoder exists and for
// design review. Replace by a real data source without touching the UI.
#pragma once
#include "model.h"

// Fixed presets for screenshots: 0 driving, 1 charging near full,
// 2 low and hot, 3 battery 2 offline, 4 the real build today
// (linked, no decoder yet).
void sim_scenario(Snapshot &s, int id);

// Continuously changing demo data for running on the device.
void sim_step(Snapshot &s, uint32_t nowMs);
