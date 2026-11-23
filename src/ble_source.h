// Live data source: connects to the single battery Bluetooth module (name
// contains "GC2RB"), which serves both batteries, and fills a Snapshot with
// whatever it can decode.
//
// Read-only: it subscribes to the module's notification characteristic and never
// writes to the module. Until the protocol decoder exists, the module shows as
// "linked, no readings" and every raw notification is printed to serial.
#pragma once
#include "model.h"

void ble_start();                  // starts the background Bluetooth task
void ble_snapshot(Snapshot &out);  // thread-safe copy of the latest state
