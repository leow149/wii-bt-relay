# Project status — read this before flashing anything

This tracks what is real/working, what is adapted-but-unverified, and what is a
placeholder stub. Nothing here has been tested against real ESP32 hardware or a
real Wii console — I (the AI that scaffolded this repo) have no way to compile
against Xtensa toolchains or talk to physical Bluetooth radios. Treat this as a
structured starting point for hands-on iteration, not a finished product.

## Audit findings (post-push review)

A full audit compared every file actually on GitHub against the local sources
used to build and verify this project. Two things were found:

1. **Real bug, now fixed**: `board-b-wii/main/CMakeLists.txt` never defined
   the `BLUERETRO` preprocessor macro that `bt_reference/zephyr_hci_defs.h`,
   `zephyr_l2cap_defs.h`, and `zephyr_types.h` branch their entire content on.
   Every local compile check during development added `-DBLUERETRO` by hand
   as a gcc flag, which masked the fact that the real CMake build never set
   it — `idf.py build` would have failed immediately on a real ESP-IDF
   toolchain. Fixed by adding `target_compile_definitions(${COMPONENT_LIB}
   PRIVATE BLUERETRO)` to the component CMakeLists. This specific line
   (`target_compile_definitions` after `idf_component_register`) is the
   standard documented ESP-IDF idiom but, like everything else touching the
   real toolchain, hasn't been run through `idf.py build` on real hardware —
   verify it works on your installed IDF version.
2. **Two purely cosmetic whitespace differences**, found and root-caused, no
   fix needed: `bt_reference/host.h` is missing one trailing blank line at
   EOF (byte-for-byte harmless), and `bt_reference/zephyr_l2cap_defs.h` has
   one fewer tab of indentation on two lines *inside an `#ifndef BLUERETRO`
   block that never compiles for this project's build* (since BLUERETRO is
   always defined here) — not just harmless but unreachable.

The takeaway: manual, ad-hoc `-DBLUERETRO`-flagged gcc compiles throughout
development were good enough to catch real logic/struct-layout bugs, but they
silently papered over one real build-configuration gap. If you make further
changes, double check the actual CMakeLists / build system, not just standalone
gcc syntax checks, before trusting that something builds for real.

## Board A — controller-facing

| Component | Status | Notes |
|---|---|---|
| Bluepad32 integration | 🟢 Should work as-is | Bluepad32 is a mature, actively maintained library. `main.cpp` follows its documented callback pattern. |
| UART output struct | 🟢 Straightforward | Simple fixed-size struct over UART, nothing exotic. |
| Build config | 🟡 Needs your board specifics | You must supply your own `platformio.ini` board target / Bluepad32 install per their docs — see README. |

Board A is the low-risk half. Get this working and tested against literally
anything (even just printing to Serial) before touching Board B.

## Board B — Wii-facing (the hard half)

| Component | Status | Notes |
|---|---|---|
| `bt_reference/*` (vendored BlueRetro) | 🟢 Real, working code (as a *host*-role stack) | Unmodified from BlueRetro. Proven to work for BlueRetro's own purpose: ESP32 as Bluetooth Classic **host** pairing outward to gamepads. **Not** proven for the peripheral/device role we need — included so you can cross-reference real HCI/L2CAP/SDP sequences. |
| `wiimote/wm_reports.{c,h}` | 🟡 Written from public spec, unverified | Report ID layout follows the WiiBrew wiki's documented format. Byte-for-byte correctness has **not** been checked against a real capture. |
| `wiimote/wm_crypto.{c,h}` | 🔴 Stub / placeholder | The Wiimote extension "encryption" scheme is real and documented in outline, but I have not verified exact constants against a live packet capture, so the transform in this file is marked `TODO_VERIFY` and should not be trusted until you check it against your own Phase 1 capture. |
| `wiimote/wm_eeprom.{c,h}` | 🔴 Placeholder data | Calibration/Mii-region layout offsets are from public docs; actual byte values are zeroed placeholders. Games that read calibration data may behave oddly until you drop in real dumped values. |
| `bt_device_role/*` | 🟡 Upgraded again — several concrete bugs fixed | Cross-referencing rnconrad/WiimoteEmulator's real, Wii-confirmed source (not copied, see NOTICE) surfaced three likely blockers that are now fixed: (1) Bluetooth address now forced to carry a Nintendo OUI via `bt_bdaddr_setup.c`, (2) Class of Device corrected from an unverified placeholder to the real `0x002504`, (3) Simple Secure Pairing is now explicitly disabled (`cmd_write_ssp_mode_disable`), since the Wii only speaks legacy PIN pairing. The PIN-reply logic was independently confirmed correct against the same source. **Still unverified**: whether these fixes are sufficient — that's only answerable with real hardware. |
| `bt_device_role/bt_bdaddr_setup.*` | 🟡 Real API, one unverified assumption | Uses the real, documented `esp_base_mac_addr_set()` / Bluetooth-address-is-base-plus-2 relationship (confirmed via Espressif's own docs, not assumed). Not yet verified: exact byte-overflow behavior if the base MAC's last octet is near 0xFF (noted inline as TODO_VERIFY, avoided in the current fixed values by construction, but not handled generally). |
| `bt_device_role/bt_sdp_device_role.*` | 🟡 New — encoding verified by hand-decoding, content coverage unverified | Implements a from-scratch SDP responder (not copied from any source — see NOTICE) for the single ServiceSearchAttributeRequest a host typically sends to browse the Wiimote's HID service. The DataElement/PDU wire encoding was checked by manually decoding a test response byte-for-byte against the SDP spec — every length field and nested structure lined up correctly. **Not yet verified**: whether the specific set of attributes included (missing HIDDescriptorList, in particular) is enough for the Wii's SDP client to accept the service. Also missing: continuation-state handling for responses that would exceed one L2CAP payload (current response is ~83 bytes, comfortably under typical MTUs, but this hasn't been stress-tested with a larger attribute set). |
| `bt_device_role/bt_l2cap_device_role.*` | 🟡 Extended to a third (SDP) channel | Same acceptor logic as before, now also handling PSM 0x0001 (SDP) alongside control/interrupt, with a generic channel-data dispatcher routing SDP channel traffic to the new responder. Same `TODO_VERIFY` caveats on proactive Configuration Request timing apply to the SDP channel as well. |
| `bt_device_role/bt_vhci_transport.*` | 🟢 Pattern verified, transport itself untestable here | Init sequence, callback registration, and packet framing are copied from BlueRetro's real `host.c`, not reconstructed from memory. Compiles cleanly against stub ESP-IDF headers matching the real API shapes. Cannot be run/tested without an actual ESP32. |
| `uart_bridge.*` (Board B side) | 🟢 Straightforward, compiles clean | Completes the Board A → Board B data path; simple framing/checksum/dedup logic, low risk. |
| Output report handling (LED/rumble) | 🔴 Not implemented | The Wii will send these once channels are open; not responding may cause it to treat the connection as unresponsive. Next thing to build after hardware validates the L2CAP/SDP layers. |

## What to actually do first

1. Flash Board A alone, confirm it reads your real controller correctly over serial.
2. Flash Board B alone (no controller, no Wii needed yet) and confirm over serial monitor that it gets through Reset → Write Local Name → Write Class of Device → Write Scan Enable without errors, and check with a phone or PC's Bluetooth scanner whether a device named "Nintendo RVL-CNT-01" becomes visible. This is real, wired code — this checkpoint is genuinely testable today.
3. Do the sniffer capture from `tools/CAPTURE_NOTES.md` (real Wiimote pairing to a PC) — needed to verify the placeholder Class of Device bytes and the PIN/address-exchange assumption in `cmd_pin_code_reply()`.
4. Try pairing a real Wii to Board B and watch the serial log / a sniffer trace to see how far the state machine gets — this is the first real hardware-in-loop test of the actual open question. With the L2CAP layer now in place, a successful run should reach `BT_DEV_STATE_STREAMING`.
5. If step 4 stalls partway through L2CAP, compare against the capture to check the `TODO_VERIFY` items in `bt_l2cap_device_role.c` (proactive Configuration Request timing, MTU/option handling).
6. Implement output report handling (LED/rumble) — not yet written; needed for the Wii to consider the connection healthy long-term, not just for initial pairing.
7. Only after streaming genuinely works, connect Board A physically and confirm end-to-end with a real controller instead of the current placeholder accelerometer values.

Skipping ahead and wiring both boards together before Board B alone can fool
the Wii will make debugging much harder, since you won't know which layer
broke.
