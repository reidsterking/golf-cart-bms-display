# Golf Cart BMS Display

Dashboard for two 48 V Epoch GC2 batteries on an ESP32 "Cheap Yellow Display" (ESP32-2432S028R, 320x240 touch).
The batteries' Bluetooth modules advertise as `GC2RB<serial>`.

## Status

| Part | State |
| --- | --- |
| Dashboard UI (overview, power graph, alerts) | Built, verified in the desktop renderer and compiles for the board. Not yet run on hardware. |
| Alert engine (debounced, hysteresis) | Built, placeholder thresholds |
| Battery data over Bluetooth | Blocked: the module only answers after the official app sends a request, and that request is not known yet. The dashboard currently runs on simulated data. |

## Environments

```bash
pio run -e cyd -t upload     # dashboard on the board (simulated data for now)
pio run -e proxy -t upload   # BLE protocol logger, see tools/proxy
pio run -e native            # desktop build that renders the UI to images
```

Render screenshots:

```bash
pio run -e native && mkdir -p sim/out && .pio/build/native/program sim/out && python3 sim/ppm2png.py sim/out 3
```

## Layout

- `src/model.h` data model; the only thing the UI depends on
- `src/ui/` LVGL dashboard
- `src/alerts.cpp` alert rules
- `src/sim_source.cpp` simulated data, to be replaced by the Bluetooth source
- `tools/proxy/` BLE man-in-the-middle logger used to learn the module protocol
- `docs/screens/` screenshots

## Board notes

If colors look inverted, build with `-DCYD_INVERT=1`. If touches land in the wrong place, adjust the `TOUCH_*` constants at the top of `src/main.cpp`.
