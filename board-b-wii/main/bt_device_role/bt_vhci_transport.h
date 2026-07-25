#ifndef BT_VHCI_TRANSPORT_H
#define BT_VHCI_TRANSPORT_H

#include <stdint.h>
#include <esp_err.h>

typedef void (*bt_vhci_rx_cb_t)(uint8_t *data, uint16_t len);

/* Brings up the ESP32 Bluetooth controller and registers for VHCI
 * send/receive, per the verified pattern described in bt_vhci_transport.c.
 * rx_cb is called with each raw received H4 packet (type byte + payload). */
esp_err_t bt_vhci_transport_init(bt_vhci_rx_cb_t rx_cb);

/* Used internally by bt_device_role.c's send_hci_cmd() and
 * bt_l2cap_device_role.c; exposed here rather than kept static so those
 * files don't need their own extern forward-declaration (as an earlier
 * draft did).
 *
 * NAMED bt_vhci_transport_send, not vhci_send -- a real ESP32 build caught
 * a symbol collision: ESP-IDF's own precompiled Bluetooth controller blob
 * (components/bt/controller/lib_esp32/esp32/libbtdm_app.a) already defines
 * a function called exactly "vhci_send", and the linker won't tolerate two
 * definitions of the same global symbol. Keep this name prefixed.
 *
 * IMPORTANT (found via real hardware test): this ENQUEUES the packet and
 * returns immediately -- it does NOT block waiting for the controller to be
 * ready. It's safe to call from anywhere, including from inside
 * bt_device_role_on_hci_event()'s call chain (the rx callback itself often
 * calls this to send the *next* command in response to a Command Complete
 * event). A real device test showed that a blocking busy-wait here
 * deadlocks: the controller's "ready for next send" notification doesn't
 * arrive until the current rx-callback chain unwinds, so waiting for it
 * from inside that same chain never succeeds. The actual transmit now
 * happens from bt_vhci_transport_pump_tx(), which must be called
 * periodically from a normal task context (see main.c's loop) -- safe to
 * block/wait there since it's not nested inside the controller's own
 * callback delivery. */
void bt_vhci_transport_send(const uint8_t *hci_cmd, uint16_t len);

/* Call periodically from a normal task context (e.g. the main loop in
 * main.c) -- NOT from within bt_device_role_on_hci_event() or anything it
 * calls. Sends at most one queued packet per call if the controller is
 * currently ready for one; a no-op otherwise. See bt_vhci_transport_send()
 * above for why the send and the queue-pump are split like this. */
void bt_vhci_transport_pump_tx(void);

#endif
