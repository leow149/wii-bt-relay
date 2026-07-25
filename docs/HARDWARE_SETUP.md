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

**Board A (`board-a-controller/`) — Arduino IDE (not the plain PlatformIO
`lib_deps` route the checked-in `platformio.ini` implies):**

Bluepad32 is **not a normal Arduino library**. It replaces part of the ESP32
Bluetooth stack, so it needs to be installed as a special combined board
package, not fetched as a dependency. The correct steps:

1. Install the Arduino IDE (2.x).
2. File → Preferences → "Additional boards manager URLs", add both of these (comma-separated):
   - `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - `https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json`
3. Tools → Board → Boards Manager: install the official **esp32** package, then search **bluepad32** and install the **"ESP32 + Bluepad32"** package.
4. Tools → Board → **ESP32 + Bluepad32 Arduino** → pick your specific dev board (if unsure, "DOIT ESP32 DEVKIT V1" is the common default for a generic ESP-WROOM-32 board).
5. Create a new sketch, paste in `board-a-controller/src/main.cpp`'s content, and copy `shared_protocol.h` into the same sketch folder (Arduino sketches don't read from arbitrary include paths the way the checked-in `platformio.ini` assumed).
6. Linux only: add yourself to the serial port group so you don't need `sudo` to flash: `sudo usermod -a -G dialout $USER`, then log out and back in.
7. Plug in Board A, select Tools → Port (usually `/dev/ttyUSB0`), click Upload.

The existing `board-a-controller/platformio.ini` in this repo reflects the
original (incorrect) plain-library assumption — treat the Arduino IDE route
above as authoritative until that's fixed to use Bluepad32's actual
ESP-IDF+PlatformIO template (`esp-idf-arduino-bluepad32-template`), which is
a materially different, more involved setup than a one-line `lib_deps` entry.

**Board B (`board-b-wii/`) — ESP-IDF, Linux:**

1. Install prerequisites: `sudo apt install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0`
2. Clone the **v5.5 branch** (not v6.0 — v6.0 removed some legacy drivers, and this project's low-level VHCI/Bluedroid-style Classic Bluetooth calls haven't been checked against that branch):
   ```
   mkdir -p ~/esp && cd ~/esp
   git clone -b release/v5.5 --recursive https://github.com/espressif/esp-idf.git
   ```
3. Install the toolchain: `cd ~/esp/esp-idf && ./install.sh esp32`
4. In every new terminal you use for this project: `. ~/esp/esp-idf/export.sh`
5. From `board-b-wii/`: `idf.py set-target esp32` then `idf.py build`
6. Plug in Board B, find its port (`ls /dev/ttyUSB*` or `ls /dev/ttyACM*`), then: `idf.py -p /dev/ttyUSB0 flash monitor` (Ctrl+] to exit the monitor)

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
