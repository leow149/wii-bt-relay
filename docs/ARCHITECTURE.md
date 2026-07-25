# Architecture

## Layering, and which layer is actually hard

It helps to keep four layers distinct, because they have very different risk
profiles:

1. **Physical controller → Board A.** Solved by Bluepad32. Not our problem.
2. **Board A → Board B transport.** A UART link carrying a small fixed-size
   struct (buttons bitmask + stick/trigger axes). Trivial, low risk.
3. **Wiimote report/data format.** What bytes a Wiimote sends for button
   presses, accelerometer, extension data, and what output reports (LED,
   rumble) it must respond to. **Well documented** — WiiBrew wiki and the
   xwiimote project's PROTOCOL doc spell this out fully. Low risk, mechanical
   work to implement.
4. **Bluetooth Classic link establishment as a device/peripheral that the Wii
   will accept.** Discoverability, class of device, the PIN/address exchange,
   SDP responses, L2CAP channel setup at the right PSMs, all closely enough
   matching what a real Wiimote's Broadcom BCM2042 chip does that the Wii's
   host stack doesn't bail out. **This is the actual unsolved problem.** A
   working example exists on Linux/BlueZ (rnconrad/WiimoteEmulator), built by
   patching BlueZ below its normal API surface — meaning even there, the
   stack's default behavior didn't match what the Wii expected out of the
   box. Nobody has published a working ESP32 equivalent.

Everything in `bt_device_role/` is aimed at layer 4. Everything in
`wiimote/` is aimed at layer 3. Keep that distinction in mind when deciding
where to spend debugging time — a bug that looks like "wrong button" is a
layer 3 problem; a bug that looks like "Wii won't even connect" is layer 4.

## Why BlueRetro's code is reference material, not a dependency

BlueRetro's `bt_reference/` files implement layers 2–4 for the **host** role
only (ESP32 initiating connections to gamepads). The device-role code needed
here is a different mode of the same underlying HCI primitives — same
opcodes, opposite direction (accepting vs. initiating, discoverable vs.
scanning, slave vs. master). Rather than surgically mutating a 2,000-line file
whose every edge case I can't test, `bt_device_role/` is written fresh,
referencing the same class of HCI commands, so its logic is auditable in
isolation. Cross-check both against a real Bluetooth capture as you bring this
up — see `tools/CAPTURE_NOTES.md`.

## Data flow at steady state (once everything works)

```
controller button/stick state
  -> Bluepad32 callback (Board A)
  -> packed into struct wm_relay_input_t
  -> UART frame (Board A -> Board B)
  -> Board B unpacks into current Wiimote input report state
  -> Wiimote input report (0x30/0x31/etc per WiiBrew) sent over the
     interrupt L2CAP channel to the Wii
  -> Wii's output reports (LED set, rumble) arrive on the same channel
  -> Board B acts on them / relays LED state back to Board A if desired
```
