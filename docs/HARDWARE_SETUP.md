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

**Board A (`board-a-controller/`) — arduino-cli, one command per flash:**

Bluepad32 is **not a normal Arduino library**. It replaces part of the ESP32
Bluetooth stack, so it's installed as a special combined board package, not
fetched as a dependency — that's why this isn't a plain PlatformIO `lib_deps`
setup (an earlier version of this doc assumed it was; that's now fixed).

`board-a-controller/` is laid out as a real Arduino sketch (the folder name
matches `board-a-controller.ino`, with `shared_protocol.h` sitting right
alongside it), so once the toolchain below is installed once, every future
build+flash is just:

```
./board-a-controller/flash.sh /dev/ttyUSB0
```

(or `make board-a PORT_A=/dev/ttyUSB0` from the repo root). `flash.sh`
compiles, uploads, and opens the serial monitor in one shot.

One-time setup:
1. Install arduino-cli:
   ```
   curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
   mv bin/arduino-cli ~/.local/bin/
   ```
2. Register the two board-manager URLs and install the combined core:
   ```
   arduino-cli config init
   arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
   arduino-cli core update-index
   arduino-cli core install esp32_bluepad32:esp32
   ```
3. Confirm the exact FQBN for your board (the package name above is a best guess — verify it):
   ```
   arduino-cli board listall | grep -i bluepad
   ```
   If it differs from `esp32_bluepad32:esp32:esp32dev`, pass the real one as `flash.sh`'s second argument.
4. Linux only, so you don't need `sudo` to flash: `sudo usermod -a -G dialout $USER`, then log out and back in.

Prefer the Arduino IDE GUI instead? Same board-manager URLs go in
File → Preferences → "Additional boards manager URLs"; install **esp32**
and **ESP32 + Bluepad32** via Boards Manager; open `board-a-controller.ino`
directly (it's already a valid sketch) and use Tools → Board / Tools → Port / Upload.

**Board B (`board-b-wii/`) — ESP-IDF, one command per flash:**

Once the toolchain below is installed once, every future build+flash is:

```
./board-b-wii/flash.sh /dev/ttyUSB0
```

(or `make board-b PORT_B=/dev/ttyUSB0` from the repo root). `flash.sh` sets
the target, builds, flashes, and opens the monitor in one shot — and will
auto-source `export.sh` for you if you pass `IDF_PATH` and haven't sourced
it yet in that shell (`IDF_PATH=$HOME/esp/esp-idf ./board-b-wii/flash.sh`).

One-time setup:
1. Install prerequisites: `sudo apt install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0`
2. Clone the **v5.5 branch** (not v6.0 — v6.0 removed some legacy drivers, and this project's low-level VHCI/Bluedroid-style Classic Bluetooth calls haven't been checked against that branch):
   ```
   mkdir -p ~/esp && cd ~/esp
   git clone -b release/v5.5 --recursive https://github.com/espressif/esp-idf.git
   ```
3. Install the toolchain: `cd ~/esp/esp-idf && ./install.sh esp32`
4. Either source `. ~/esp/esp-idf/export.sh` once per shell yourself, or just always invoke `flash.sh` with `IDF_PATH=$HOME/esp/esp-idf` set.

## Why there isn't one single command for both boards

Board A (arduino-cli) and Board B (ESP-IDF) are two unrelated toolchains
with no shared build system, so unifying them into one literal command
would just be a thin wrapper hiding which tool actually ran — and you
should be flashing and checking each board independently before wiring
them together anyway (see STATUS.md's bring-up order). One command *per
board*, via `flash.sh` or the root `Makefile`, is the practical ceiling
here.

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
