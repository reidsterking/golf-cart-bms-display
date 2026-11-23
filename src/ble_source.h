// Live data source: connects to the two battery Bluetooth modules (name contains
// "GC2RB") and fills a Snapshot with whatever it can decode.
//
// Read-only: it subscribes to the module's notification characteristic and never
// writes to the modules. Until the protocol decoder exists, batteries show as
// "linked, no readings" and every raw notification is printed to serial.
#pragma once
#include "model.h"

void ble_start();                  // starts the background Bluetooth task
void ble_snapshot(Snapshot &out);  // thread-safe copy of the latest state
