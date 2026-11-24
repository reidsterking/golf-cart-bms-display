# Handoff: golf cart battery display

Written for a Claude session that runs directly on the user's Mac with access to the board and Bluetooth.

## Goal
A professional-looking dashboard on an ESP32 "Cheap Yellow Display" (ESP32-2432S028R, 320x240 touch, chip ESP32-D0WD-V3) mounted in a golf cart. It must show, for two 48 V Epoch GC2 lithium batteries: charge of each, current the cart is pulling, remaining energy, temperature (Fahrenheit), electrical activity, and warnings. The user wants warnings that are not noisy (the official app nags) and is concerned about not continuously charging the batteries.

## User preferences
Be direct and honest, challenge weak assumptions, rate ideas out of 10, say when unsure. Never use em dashes in anything written, including code comments and commits. The user wants a real working result, not a demo, and is understandably frustrated by round trips.

## What is done and working
- Dashboard UI (LVGL 8.3): Overview, Power graph, Alerts. Fahrenheit. Alerts auto-jump to the Alerts page on a new or worse alert, flash until muted by tapping the row, then the UI stays wherever the user goes until another new alert. Title "EPOCH 48V". Verified in a desktop renderer and on the real board (demo build looked good).
- Alert engine with debounce (src/alerts.cpp). Thresholds and the 30 Ah capacity are placeholders.
- Live Bluetooth source (src/ble_source.cpp): connects to the one module, read-only, `decode()` is an empty stub. Default `cyd` build shows `--` and "no readings" alerts, never fake data. `cyd-demo` build shows simulated data labeled DEMO DATA.
- Repo: github.com/reidsterking/golf-cart-bms-display (private), branch main.

## The blocker: the module's protocol is unknown
There is ONE Bluetooth module serving BOTH batteries. It advertises as `GC2RB48300087` (the name ends with a carriage return, 0x0D), address 10:55:44:36:40:49, advertising service 0xFFE0 and manufacturer data `FF FF 10 55 44 36 40 49`. The name suggests 48 V, 30 Ah, serial 0087 (an inference).

GATT as read from the module:
- Service 0xFFE0: FFE1, FFE2, FFE3 (read, write-no-response, 128 bytes), FFE4 (notify), FFE5 (read, 128 bytes), FFE6 (read, write, write-no-response, 128 bytes). All reads were zeros.
- Service f000ffc0 (TI over-the-air firmware update): ffc1, ffc2. NEVER WRITE TO THESE. A bad write could brick the module.
- Standard 0x1800 and 0x1801 services.

The module accepts only one connection at a time.

Ruled out so far:
- The module publishing readings by itself: subscribed to FFE4 for 60 s, polled every readable slot for 34 s, nothing changed.
- Active probe (tools/probe_active): six read-type request frames (Pylontech-style ASCII for addresses 1 and 2, protocol version, JBD basic info, Modbus read, bare CR) sent to FFE1, FFE2, FFE3 and FFE6. No reaction at all. Do not repeat or widen this blindly; unknown writes to a battery system carry risk.
- Reading the Epoch Pro app: it is a native Swift iOS app (installed on the Mac at /Applications/Epoch Pro.app, from the Mac App Store iPhone and iPad tab). No readable script bundles, no service UUID strings outside the encrypted binary.
- PacketLogger on the Mac (from Additional Tools for Xcode 26.6 RC): opened, said it was recording, showed no rows even while Epoch Pro was connected to the module. The macOS Bluetooth logging profile was not confirmed installed.
- Android decompile: no Android build of Epoch Pro found; the only Android Epoch app is "Epoch Li-ion" (package yep.epoch), likely a different product.

Still open:
1. Relay (tools/proxy): board connects to the real module, advertises a clone (same name, service, manufacturer data), forwards and logs everything the app writes. The Epoch Pro app on the Mac did not see the clone. It was never confirmed whether the clone is visible to ANY scanner. Next step: scan from the Mac. tools/mac_scan.py needs `bleak==0.22.3` with `pyobjc-*<11` (the Mac's system Python 3.9 cannot build pyobjc 12). Or use the LightBlue app. Compare the clone's advertisement with the real module's (the real one puts the name in the scan response and service plus manufacturer data in the primary packet).
2. Capture with an Android device later (user will borrow one). Needs Epoch Pro to exist on Android, unconfirmed. tools/btsnoop_att.py reads btsnoop_hci.log with no dependencies; tested only on a synthetic file.
3. iPhone capture via PacketLogger "New iOS Trace" with Apple's iPhone Bluetooth logging profile.
4. Plan B if Bluetooth stays a dead end: a current sensor on the main cable plus a voltage input for real total volts, amps and power. No per-battery data, needs high-current wiring.
5. Ask Epoch support for the protocol (low odds).

## Environment facts (the Mac)
- Apple silicon MacBook Air, system Python 3.9.6, no Homebrew, no gh CLI.
- PlatformIO Core 6.2.0 via `pip install --user` (PATH set in ~/.zshrc), platform espressif32 5.4.0, NimBLE-Arduino 1.4.3.
- Board serial port: /dev/cu.usbserial-110. Always pass `--upload-port` and `--port`. A leftover monitor process often holds the port; check with `lsof /dev/cu.usbserial-110`.
- Before any test that needs the module: quit Epoch Pro (Cmd+Q), toggle the Mac's Bluetooth off and on, keep the phone's Bluetooth off, and make sure the module is powered (the cart/batteries on).
- Terminal paste problems have been common: give single commands and keep them short.

## PlatformIO environments
`cyd` (live, default), `cyd-demo`, `proxy` (relay logger), `probe` (read-only poller), `probe-active` (the six frames), `native` (desktop screenshots: `pio run -e native && .pio/build/native/program sim/out && python3 sim/ppm2png.py sim/out 3`).

## Suggested first moves for the new session
1. Run the relay (`pio run -e proxy -t upload ...`) and scan from the Mac to see whether the clone is visible and what it advertises versus the real module. Fix the clone until the Epoch Pro app on the Mac lists and connects to it, then collect the writes it makes (lines starting APP>MOD).
2. If the app can never be made to accept the clone, pursue a capture (iPhone trace or the Android route).
3. Once requests and replies are known, write `decode()` in src/ble_source.cpp (it must fill both batteries, their own serial numbers, volts, amps, SOC, temperature, remaining Ah), replace the placeholder thresholds in src/alerts.cpp with real limits, and confirm on the display.
4. Never present simulated numbers as real.
