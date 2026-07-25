# wii-bt-relay

Two-ESP32 project to let a real Bluetooth game controller (Xbox/PS/Switch Pro/
8BitDo/etc.) drive a Nintendo Wii, by having one board pair to the controller
and a second board impersonate a Wiimote to the console.

**Read `docs/STATUS.md` before doing anything else.** This repo mixes solid,
reusable code with genuinely experimental, untested code, and that file marks
which is which. The short version: Board A (controller-facing) should just
work. Board B (Wii-facing) is where the actual unsolved engineering is, and no
public project currently does this successfully on ESP32 — this repo is a
structured starting point for that work, not a finished adapter.

## Why two boards

Bluetooth Classic host role (talking to your controller) and device/peripheral
role (pretending to be a Wiimote to the Wii) are handled by separate firmware
here rather than crammed onto one chip, both to keep each side's Bluetooth
stack simple and because attempts to run simultaneous host+device classic BT
roles on a single ESP32 have shown real instability in community reports.

```
[Your Bluetooth controller]  <--BT-->  [Board A: Bluepad32 host]
                                              |
                                         UART/SPI (buttons+axes struct)
                                              |
[Nintendo Wii]  <----Bluetooth Classic---->  [Board B: Wiimote device emulator]
```

## Repo layout

```
board-a-controller/     Real Arduino sketch (arduino-cli), built on Bluepad32
  flash.sh               One-command compile + upload + monitor
board-b-wii/            ESP-IDF firmware for the Wii-facing device role
  flash.sh               One-command build + flash + monitor
  main/bt_reference/    Vendored, UNMODIFIED BlueRetro host-role BT stack
                         (Apache-2.0, see NOTICE) — read for reference, not
                         built directly into the device-role firmware
  main/bt_device_role/  New, experimental device-role HCI/L2CAP/SDP code
  main/wiimote/         Wiimote report format, crypto, and calibration data
docs/                   Architecture notes, protocol notes, status tracker
tools/                  Notes on capturing a reference Bluetooth trace
Makefile                `make board-a` / `make board-b` from the repo root
```

## Prerequisites

See `docs/HARDWARE_SETUP.md` for the full bill of materials, wiring diagram,
and toolchain install steps. Short version:
- Two genuine original ESP32 boards (not S3/C3/C6 — those lack Bluetooth
  Classic/BR-EDR, which both Wiimotes and most modern gamepads require)
- A real Bluetooth controller to pair to Board A
- A Bluetooth HCI sniffer setup (a PC with `btmon`/Wireshark) — you'll need
  this constantly while bringing up Board B; see `tools/CAPTURE_NOTES.md`
- ESP-IDF toolchain for Board B; arduino-cli (or Arduino IDE) + the
  "ESP32 + Bluepad32" board package for Board A — see HARDWARE_SETUP.md,
  Bluepad32 is not a normal library

## Build order

See `docs/STATUS.md` → "What to actually do first." Do not skip ahead to
wiring both boards together — bring Board B up alone against a real Wii
first. Once each board's one-time toolchain setup is done (HARDWARE_SETUP.md),
day-to-day iteration is just `./board-a-controller/flash.sh <port>` and
`./board-b-wii/flash.sh <port>` (or `make board-a` / `make board-b`).

## License

Apache-2.0. See `LICENSE` and `NOTICE` — this project vendors and references
code from BlueRetro (archived Dec 2025, apache-2.0 licensed, forking explicitly
invited by its author).
