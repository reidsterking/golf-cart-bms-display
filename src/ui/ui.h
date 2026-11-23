// Dashboard UI for a 320x240 landscape display. Hardware independent: it only
// reads the data model, so the same code runs on the device and in the desktop
// screenshot renderer.
#pragma once
#include "../model.h"

enum UiPage { PAGE_OVERVIEW = 0, PAGE_POWER = 1, PAGE_ALERTS = 2 };

void ui_init();
// Call at about 0.5 Hz with fresh data. Each call adds one point to the power graph.
void ui_update(const Snapshot &s, const Alerts &alerts);
void ui_goto(int page);
