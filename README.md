# Golf Cart BMS Display

ESP32 "Cheap Yellow Display" dashboard for two 48 V Epoch G2C batteries, read over their Bluetooth modules (BLE name prefix `asGC2RB`).

## Phase 1: protocol explorer (current)

`src/main.cpp` scans for `asGC2RB*` modules, connects to up to two, prints every service and characteristic, and logs all notifications as hex.

```bash
pio run -t upload
pio device monitor
```

Close the battery's phone app first. Many BLE modules accept only one connection.

Save the serial output. It is the input for the real decoder.

## Roadmap

1. Protocol discovery (this phase)
2. Decoder with host-side unit tests (`pio test -e native`)
3. Touch UI: per-battery charge, pack current, remaining capacity, temperature, warnings
4. Charge-state alerts
