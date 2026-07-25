# Getting a reference Bluetooth capture

Nearly every `TODO_VERIFY` in this repo depends on this. Do this before
debugging `bt_device_role/` against a real Wii — you need ground truth to
compare against.

## What you need
- A real Wiimote
- A PC with a Bluetooth Classic adapter you can put into monitor mode
- Linux: `btmon` (from `bluez`) is the easiest path
- Windows: see the equivalent notes in BlueRetro's own wiki
  (`Bluetooth-HCI-trace-with-Win10.md`, not vendored here, but findable in
  the archived github.com/darthcloud/BlueRetro/wiki if you want a reference)

## Steps

1. `sudo btmon -w wiimote_pair.pcap &`
2. Put the Wiimote in discoverable mode (press the red sync button under the
   battery cover, or hold 1+2).
3. Pair it to the PC as you normally would.
4. Press a few buttons, move it around a bit (for accelerometer data), then
   disconnect.
5. `sudo killall btmon`
6. Open `wiimote_pair.pcap` in Wireshark, filter on `hci_evt` and `btl2cap`.

## What to extract and cross-reference against this repo

| What to find in the capture | Where it matters in this repo |
|---|---|
| The exact Class of Device bytes the Wiimote advertises | `bt_device_role.c` → `cmd_write_class_of_device()` placeholder |
| The PIN Code Request event and what value the pairing PC replies with | `bt_device_role.c` → `cmd_pin_code_reply()` — confirm the reversed-address logic actually matches |
| The SDP request/response the Wii-side (or your PC, standing in for it) sends when browsing services | Not yet implemented in this repo — L2CAP/SDP channel setup is still a gap, see `docs/ARCHITECTURE.md` |
| The exact byte layout of a few 0x30/0x31 input reports as buttons are pressed | `wiimote/wm_reports.h` — confirm the core-buttons bit table |
| Bytes in an LED-set (0x11) or rumble (0x10) output report | Not yet implemented — output report handling is a TODO in `bt_device_role.c` |

## A cheaper first checkpoint

Before wiring up a full Wii-facing capture, you can get a partial signal
much sooner: put `bt_device_role`'s discoverability code (name + class of
device + scan enable) on the ESP32 alone, and see whether *any* generic
Bluetooth Classic scanner (a phone, a PC's Bluetooth settings) sees a device
named "Nintendo RVL-CNT-01" at all. If it doesn't show up, you have a bug in
layer 4 before you've even involved a Wii.
