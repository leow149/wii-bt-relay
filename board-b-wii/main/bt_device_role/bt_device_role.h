/*
 * EXPERIMENTAL / UNVERIFIED — read docs/STATUS.md before trusting anything
 * in this file.
 *
 * This attempts to make an ESP32 discoverable and connectable as a Bluetooth
 * Classic peripheral impersonating a Wiimote (name "Nintendo RVL-CNT-01",
 * matching class of device), and to accept the Wii's incoming connection as
 * SLAVE rather than initiating one as master — the opposite of what
 * BlueRetro's vendored host-role code in ../bt_reference does.
 *
 * It is written against standard Bluetooth Core Specification HCI opcodes
 * (these are public spec, not vendor-specific) via ESP-IDF's VHCI interface,
 * following the same low-level, bypass-Bluedroid-profiles approach that
 * BlueRetro's reference code uses — rather than ESP-IDF's higher-level
 * esp_hidd profile API, since our own earlier research turned up a
 * first-hand report that porting a working device-role Wiimote emulator to
 * ESP32 would likely require exactly this kind of low-level rewrite, with
 * the PIN/address exchange flagged as the specific hard part.
 *
 * WHAT IS NOT DONE HERE: exact VHCI callback registration and event buffer
 * handling are ESP-IDF-version-specific (esp_bt.h / esp_vhci_host_callback_t
 * signatures have shifted across IDF releases). This file sketches the
 * command sequence and state machine; wiring it to your exact IDF version's
 * VHCI entry points is left as the first real implementation task — see the
 * TODO markers in bt_device_role.c.
 */
#ifndef BT_DEVICE_ROLE_H
#define BT_DEVICE_ROLE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BT_DEV_STATE_INIT = 0,
    BT_DEV_STATE_RESET_SENT,
    BT_DEV_STATE_NAME_SET,          /* HCI_Write_Local_Name done */
    BT_DEV_STATE_COD_SET,           /* HCI_Write_Class_of_Device done */
    BT_DEV_STATE_SCAN_ENABLED,      /* HCI_Write_Scan_Enable: page+inquiry scan on */
    BT_DEV_STATE_IAC_LAP_SET,       /* HCI_Write_Current_IAC_LAP: GIAC+LIAC registered --
                                     * needed so a Limited Inquiry (e.g. the Wii's own
                                     * sync-button scan) finds us at all, not just a
                                     * General Inquiry. See bt_device_role.c. */
    BT_DEV_STATE_SSP_DISABLED,      /* HCI_Write_Simple_Pairing_Mode(0): legacy PIN pairing only */
    BT_DEV_STATE_WAIT_CONN_REQUEST, /* Discoverable, waiting for the Wii to page us */
    BT_DEV_STATE_ACCEPTING_CONN,    /* HCI_Accept_Connection_Request sent, role=slave */
    BT_DEV_STATE_PIN_EXCHANGE,      /* Handling HCI_PIN_Code_Request_Event */
    BT_DEV_STATE_LINK_ESTABLISHED,  /* ACL link up, ready for L2CAP */
    BT_DEV_STATE_L2CAP_CTRL_OPEN,   /* Control channel, PSM 0x11, accepted */
    BT_DEV_STATE_L2CAP_INTR_OPEN,   /* Interrupt channel, PSM 0x13, accepted */
    BT_DEV_STATE_STREAMING,         /* Sending input reports, handling output reports */
    BT_DEV_STATE_ERROR,
} bt_device_role_state_t;

/* Standard Bluetooth Core Spec device class for a generic HID peripheral,
 * as commonly used for the Wiimote's advertised Class of Device.
 * VERIFY the exact 3-byte value against your own capture (tools/CAPTURE_NOTES.md)
 * before assuming this specific constant is what a real Wiimote sends. */
#define WM_DEVICE_NAME "Nintendo RVL-CNT-01"

/* PSMs the Wii expects for the two Wiimote L2CAP channels, per WiiBrew/
 * xwiimote's documented protocol — these two values are well-established
 * across multiple independent sources, unlike most of the rest of this file. */
#define WM_L2CAP_PSM_CONTROL   0x11
#define WM_L2CAP_PSM_INTERRUPT 0x13

void bt_device_role_init(void);
bt_device_role_state_t bt_device_role_get_state(void);

/* Call with each raw H4 packet received from bt_vhci_transport's rx
 * callback (see bt_vhci_transport.h and main.c for the wiring). */
void bt_device_role_on_hci_event(uint8_t *data, uint16_t len);

/* Call periodically (or from the VHCI receive callback) to pump the state
 * machine forward as HCI events arrive. */
void bt_device_role_poll(void);

/* Feed the latest controller state (from wm_reports.h's mapping of Board A's
 * UART frames) into the current outgoing input report. Only meaningful once
 * state == BT_DEV_STATE_STREAMING. */
void bt_device_role_update_buttons(uint16_t relay_buttons, int8_t stick_x, int8_t stick_y);

#endif
