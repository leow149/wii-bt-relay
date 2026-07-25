# Hardware setup

This consolidates everything from `docs/STATUS.md`'s bring-up plan into one
concrete shopping list + wiring guide. Read `docs/STATUS.md` too — this file
is "what to buy and wire," that one is "what order to bring it up in and
what's actually verified."

## Bill of materials

| Item | Qty | Notes |
|---|---|---|
| ESP32-DevKitC-32E (ESP-WROOM-32 module) | 2 | **Must be the original ESP32.** Not S3, C3, C6, or H2 — those chips dropped Bluetooth Classic (BR/EDR) support entirely, and both real Wiimotes and most modern gamepads (PS4/PS5, Switch Pro, most 8BitDo) only speak Classic, not BLE. |
| USB cable (per board) | 2 | For flashing + serial monitor. |
| Jumper wires (M-M, breadboard-style) | 3 | For the Board A ↔ Board B UART link. |
| Bluetooth game controller | 1 | Xbox, PS4/PS5, Switch Pro, 8BitDo, etc. If it's a newer Xbox controller on firmware v5.x+, note it's actually BLE, not Classic — should still work via Bluepad32's BLE support, but it's the one common exception worth knowing about. |
| Nintendo Wii console | 1 | Needed once you're past Board-B-alone bring-up (see STATUS.md). Not needed for the earliest checkpoints. |

Two boards ≈ $12–20 total; everything else is incidental.

## Wiring: Board A ↔ Board B UART link

Only three wires, no special connector:

| Board A pin | Board B pin | Purpose |
|---|---|---|
| GPIO17 (TX) | GPIO16 (RX) | Board A → Board B data |
| GND | GND | Shared ground reference |

(Board A's RX/GPIO16 is unused in the current one-directional design — Board
B never talks back to Board A — so it's not wired. If you later want Board B
to relay LED/rumble state back to Board A, that's the wire to add.)

Both boards are powered independently over their own USB cables; there's no
shared power rail needed for the UART link itself.

## Software / toolchain

**Board A (`board-a-controller/`) — Arduino-style:**
1. Install the Arduino IDE (or PlatformIO, if you prefer — a `platformio.ini` is already in that folder).
2. Add the ESP32 board package via Arduino's Boards Manager.
3. Install the **Bluepad32** library (via Library Manager, or see https://bluepad32.readthedocs.io for the manual install path — Bluepad32 needs a bit more setup than a typical Arduino library, since it replaces part of the Bluetooth stack).
4. Open `board-a-controller/src/main.cpp`, select your board, flash.

**Board B (`board-b-wii/`) — ESP-IDF:**
1. Install ESP-IDF per Espressif's official get-started guide (this is a real embedded toolchain install, not a quick library add — budget some time for it if you haven't done it before).
2. From `board-b-wii/`, the usual ESP-IDF flow applies: `idf.py set-target esp32`, `idf.py build`, `idf.py -p <port> flash monitor`.
3. Watch the serial monitor — `docs/STATUS.md`'s checkpoint 2 tells you what a successful bring-up should print.

## Bring-up order (short version — see STATUS.md for the full version)

1. Flash Board A alone → confirm it reads your controller over serial.
2. Flash Board B alone → confirm it reaches `BT_DEV_STATE_WAIT_CONN_REQUEST` in the logs, and a phone/PC Bluetooth scan shows "Nintendo RVL-CNT-01".
3. Put the Wii in "sync a new controller" mode → see how far Board B's state machine gets.
4. Only once that's promising, wire the two boards together and test end-to-end with a real controller.

## If something doesn't work

`docs/STATUS.md` has a per-module breakdown of what's verified vs. still
experimental — that's the first place to check when something in the
pairing sequence stalls. `tools/CAPTURE_NOTES.md` covers getting a real
Bluetooth HCI capture if you need to compare byte-for-byte against what a
real Wiimote or a known-working emulator does.
