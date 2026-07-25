#ifndef BT_VHCI_TRANSPORT_H
#define BT_VHCI_TRANSPORT_H

#include <stdint.h>
#include <esp_err.h>

typedef void (*bt_vhci_rx_cb_t)(uint8_t *data, uint16_t len);

/* Brings up the ESP32 Bluetooth controller and registers for VHCI
 * send/receive, per the verified pattern described in bt_vhci_transport.c.
 * rx_cb is called with each raw received H4 packet (type byte + payload). */
esp_err_t bt_vhci_transport_init(bt_vhci_rx_cb_t rx_cb);

/* Used internally by bt_device_role.c's send_hci_cmd(); exposed here rather
 * than kept static so bt_device_role.c doesn't need its own extern
 * forward-declaration (as the earlier draft of that file did). */
void vhci_send(const uint8_t *hci_cmd, uint16_t len);

#endif
