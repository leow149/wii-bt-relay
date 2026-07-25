/*
 * EXPERIMENTAL / UNVERIFIED — see bt_device_role.h and docs/STATUS.md.
 *
 * Command construction uses REAL, verified opcodes and parameter struct
 * layouts pulled directly from ../bt_reference/zephyr_hci_defs.h (Zephyr
 * Project's hci.h, as vendored inside BlueRetro's own tree). Event parsing
 * below (Connection Request/Complete, PIN Code Request, Command Complete)
 * also now uses real field layouts from the same header, cross-checked
 * against the event structs used by BlueRetro's own working event handler.
 * VHCI transport wiring (bt_vhci_transport.c) is likewise copied from
 * BlueRetro's real, working host.c init sequence.
 *
 * What is still NOT verified: whether this sequence and this specific
 * ESP32 Bluedroid/VHCI stack behaves closely enough to a real Wiimote's
 * BCM2042 for a Wii to actually accept it — that is the open question this
 * whole project exists to answer, and it can only be resolved by testing
 * against real hardware with the capture workflow in tools/CAPTURE_NOTES.md.
 * L2CAP channel setup (needed once BT_DEV_STATE_LINK_ESTABLISHED is reached)
 * is also not yet implemented — see docs/ARCHITECTURE.md.
 */
#include <string.h>

/* zephyr_hci_defs.h expects this macro to already be defined — in
 * BlueRetro's own tree it lives in adapter/adapter.h, a file we did not
 * vendor since it's unrelated to the HCI layer. Defining it here directly. */
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

#include "bt_device_role.h"
#include "zephyr_hci_defs.h"
#include "zephyr_l2cap_defs.h"
#include "bt_vhci_transport.h"
#include "bt_l2cap_device_role.h"
#include "wm_reports.h"

/* Minimal packet view for received ACL data packets: h4 type + acl header +
 * l2cap header + payload. Same rationale as bt_hci_evt_pkt_view below —
 * avoids pulling in BlueRetro's full bt_hci_pkt union and its L2CAP/SDP/SMP/
 * ATT/HIDP substructs we don't need. */
struct bt_hci_acl_pkt_view {
    struct bt_hci_h4_hdr h4_hdr;
    struct bt_hci_acl_hdr acl_hdr;
    struct bt_l2cap_hdr l2cap_hdr;
    uint8_t data[255];
} __packed;

/* Minimal packet view for received events: just h4 type + evt header + raw
 * payload, rather than pulling in BlueRetro's full bt_hci_pkt union from
 * host.h (which also drags in L2CAP/SDP/SMP/ATT/HIDP substructs we don't
 * need yet and haven't vendored). Field layout matches the same H4 framing
 * those structs use — bt_hci_h4_hdr and bt_hci_evt_hdr come straight from
 * zephyr_hci_defs.h, so this stays consistent with the real spec without
 * the extra dependency surface. */
struct bt_hci_evt_pkt_view {
    struct bt_hci_h4_hdr h4_hdr;
    struct bt_hci_evt_hdr evt_hdr;
    uint8_t data[255];
} __packed;

static bt_device_role_state_t g_state = BT_DEV_STATE_INIT;
static uint16_t g_acl_handle = 0;
static bt_addr_t g_peer_addr;

bt_device_role_state_t bt_device_role_get_state(void) { return g_state; }

static void bt_device_role_handle_hci_event(uint8_t *data, uint16_t len);
static void bt_device_role_handle_acl_data(uint8_t *data, uint16_t len);

/* -- Command builders, using real struct layouts from zephyr_hci_defs.h -- */

static void send_hci_cmd(uint16_t opcode, const void *params, uint8_t param_len) {
    uint8_t pkt[3 + 255];
    pkt[0] = 0x01; /* H4 packet type: Command */
    pkt[1] = opcode & 0xff;
    pkt[2] = (opcode >> 8) & 0xff;
    pkt[3] = param_len;
    if (param_len) memcpy(&pkt[4], params, param_len);
    vhci_send(pkt, 4 + param_len);
}

static void cmd_reset(void) {
    send_hci_cmd(BT_HCI_OP_RESET, NULL, 0);
    g_state = BT_DEV_STATE_RESET_SENT;
}

static void cmd_write_local_name(void) {
    struct bt_hci_cp_write_local_name cp;
    memset(&cp, 0, sizeof(cp));
    strncpy((char *)cp.local_name, WM_DEVICE_NAME, sizeof(cp.local_name) - 1);
    send_hci_cmd(BT_HCI_OP_WRITE_LOCAL_NAME, &cp, sizeof(cp));
    g_state = BT_DEV_STATE_NAME_SET;
}

static void cmd_write_class_of_device(void) {
    struct bt_hci_cp_write_class_of_device cp;
    /* Confirmed against rnconrad/WiimoteEmulator's adapter.c, which is
     * known to pair with real Wii/vWii consoles: wiimote_class = 0x002504.
     * Wire bytes are little-endian per the 24-bit CoD field, so byte0
     * (LSB) = 0x04, byte1 = 0x25, byte2 (MSB) = 0x00. Previously this was
     * an unverified placeholder (0x04, 0x05, 0x00) — now corrected. */
    cp.dev_class[0] = 0x04;
    cp.dev_class[1] = 0x25;
    cp.dev_class[2] = 0x00;
    send_hci_cmd(BT_HCI_OP_WRITE_CLASS_OF_DEVICE, &cp, sizeof(cp));
    g_state = BT_DEV_STATE_COD_SET;
}

static void cmd_write_ssp_mode_disable(void) {
    /* The Wii only speaks legacy PIN-based pairing. rnconrad's adapter.c
     * explicitly disables Simple Secure Pairing mode before anything else
     * (set_up_simple_pairing_mode(dd) writing 0) — without this, a modern
     * Bluetooth controller may default to SSP and never get as far as our
     * PIN reply logic at all. */
    struct bt_hci_cp_write_ssp_mode cp;
    cp.mode = 0x00;
    send_hci_cmd(BT_HCI_OP_WRITE_SSP_MODE, &cp, sizeof(cp));
    g_state = BT_DEV_STATE_SSP_DISABLED;
}

static void cmd_write_scan_enable(void) {
    struct bt_hci_cp_write_scan_enable cp;
    cp.scan_enable = 0x03; /* bit0 = inquiry scan, bit1 = page scan: both on
                             * -> discoverable AND connectable */
    send_hci_cmd(BT_HCI_OP_WRITE_SCAN_ENABLE, &cp, sizeof(cp));
    g_state = BT_DEV_STATE_SCAN_ENABLED;
}

static void cmd_accept_connection_request(const bt_addr_t *bdaddr) {
    struct bt_hci_cp_accept_conn_req cp;
    cp.bdaddr = *bdaddr;
    cp.role = 0x01; /* 0x01 = remain SLAVE. This is the key flip versus
                      * BlueRetro's host-role code, which hardcodes 0x00
                      * (become master) at the equivalent call site in
                      * ../bt_reference/hci.c. */
    send_hci_cmd(BT_HCI_OP_ACCEPT_CONN_REQ, &cp, sizeof(cp));
    g_state = BT_DEV_STATE_ACCEPTING_CONN;
}

static void cmd_pin_code_reply(const bt_addr_t *bdaddr) {
    /* Per the xwiimote PROTOCOL doc, the expected PIN when the Wii's sync
     * button initiated pairing is the WII'S OWN bluetooth address in
     * reversed byte order — NOT the host's address, and not a fixed digit
     * PIN. This is the specific step flagged throughout our research as the
     * hard part. TODO_VERIFY against a live capture: which address, which
     * byte order, and whether Bluedroid/VHCI even lets you reply with a
     * "PIN" this unusual without additional massaging. */
    struct bt_hci_cp_pin_code_reply cp;
    cp.bdaddr = *bdaddr;
    cp.pin_len = 6;
    memset(cp.pin_code, 0, sizeof(cp.pin_code));
    for (int i = 0; i < 6; i++) {
        cp.pin_code[i] = bdaddr->val[5 - i]; /* reversed byte order, per xwiimote doc */
    }
    send_hci_cmd(BT_HCI_OP_PIN_CODE_REPLY, &cp, sizeof(cp));
    g_state = BT_DEV_STATE_PIN_EXCHANGE;
}

void bt_device_role_init(void) {
    g_state = BT_DEV_STATE_INIT;
    /* bt_vhci_transport_init() itself calls back into us via
     * bt_device_role_on_hci_event() once packets start arriving; the
     * Reset command below is sent only after the transport is up. */
    cmd_reset();
    /* Remaining bring-up (name/CoD/scan enable) is driven from the Command
     * Complete event for Reset in bt_device_role_on_hci_event() below, since
     * each HCI command must wait for its predecessor's completion event
     * before the controller will accept the next one. */
}

/* Registered with bt_vhci_transport_init() as the rx callback (see main.c).
 * data/len here is the raw H4 packet exactly as the controller delivered it.
 * Despite the name (kept for compatibility with earlier wiring/docs), this
 * now dispatches BOTH HCI events and ACL data packets — the Wii's L2CAP
 * signaling arrives as ACL data, not as HCI events. */
void bt_device_role_on_hci_event(uint8_t *data, uint16_t len) {
    if (len < 1) return;
    uint8_t h4_type = data[0];

    if (h4_type == BT_HCI_H4_TYPE_EVT) {
        bt_device_role_handle_hci_event(data, len);
    } else if (h4_type == BT_HCI_H4_TYPE_ACL) {
        bt_device_role_handle_acl_data(data, len);
    }
    /* SCO and other H4 types not relevant to this stack. */
}

static void bt_device_role_handle_hci_event(uint8_t *data, uint16_t len) {
    struct bt_hci_evt_pkt_view *pkt = (struct bt_hci_evt_pkt_view *)data;

    if (len < sizeof(pkt->h4_hdr) + sizeof(pkt->evt_hdr)) return;

    switch (pkt->evt_hdr.evt) {
        case BT_HCI_EVT_CMD_COMPLETE: {
            struct bt_hci_evt_cmd_complete *cc = (struct bt_hci_evt_cmd_complete *)pkt->data;
            /* Dispatch purely on our own state, not on cc->opcode — we only
             * ever have one command in flight at a time in this simple
             * sequential bring-up, so we don't need to check which opcode
             * completed. (cc is still parsed via the real struct so this is
             * easy to extend into a per-opcode dispatch later if commands
             * ever get pipelined.) */
            (void)cc;
            if (g_state == BT_DEV_STATE_RESET_SENT) cmd_write_local_name();
            else if (g_state == BT_DEV_STATE_NAME_SET) cmd_write_class_of_device();
            else if (g_state == BT_DEV_STATE_COD_SET) cmd_write_scan_enable();
            else if (g_state == BT_DEV_STATE_SCAN_ENABLED) cmd_write_ssp_mode_disable();
            else if (g_state == BT_DEV_STATE_SSP_DISABLED) g_state = BT_DEV_STATE_WAIT_CONN_REQUEST;
            break;
        }

        case BT_HCI_EVT_CONN_REQUEST: {
            struct bt_hci_evt_conn_request *cr = (struct bt_hci_evt_conn_request *)pkt->data;
            g_peer_addr = cr->bdaddr;
            cmd_accept_connection_request(&g_peer_addr);
            break;
        }

        case BT_HCI_EVT_PIN_CODE_REQ: {
            struct bt_hci_evt_pin_code_req *pr = (struct bt_hci_evt_pin_code_req *)pkt->data;
            /* Per the xwiimote PROTOCOL doc, use the WII'S bdaddr as the PIN,
             * not necessarily the one echoed in this event — TODO_VERIFY
             * against a live capture whether pr->bdaddr here is in fact the
             * Wii's own address (it should be, per the HCI spec, but this
             * is exactly the kind of assumption to double-check first). */
            cmd_pin_code_reply(&pr->bdaddr);
            break;
        }

        case BT_HCI_EVT_CONN_COMPLETE: {
            struct bt_hci_evt_conn_complete *cc = (struct bt_hci_evt_conn_complete *)pkt->data;
            if (cc->status == 0x00) {
                g_acl_handle = cc->handle;
                g_state = BT_DEV_STATE_LINK_ESTABLISHED;
                bt_l2cap_device_role_init(g_acl_handle);
            } else {
                g_state = BT_DEV_STATE_ERROR;
            }
            break;
        }

        default:
            break;
    }
}

/* ACL data packets carrying L2CAP traffic. Only the signaling channel
 * (cid == BT_L2CAP_CID_BR_SIG) is handled here — once channels are open,
 * data on the control/interrupt channels themselves (e.g. the Wii's output
 * reports: LED, rumble) would also arrive this way, but that dispatch isn't
 * implemented yet. See docs/STATUS.md. */
static void bt_device_role_handle_acl_data(uint8_t *data, uint16_t len) {
    struct bt_hci_acl_pkt_view *pkt = (struct bt_hci_acl_pkt_view *)data;

    if (len < sizeof(pkt->h4_hdr) + sizeof(pkt->acl_hdr) + sizeof(pkt->l2cap_hdr)) return;

    if (pkt->l2cap_hdr.cid == BT_L2CAP_CID_BR_SIG) {
        uint16_t payload_len = len - (sizeof(pkt->h4_hdr) + sizeof(pkt->acl_hdr) + sizeof(pkt->l2cap_hdr));
        bt_l2cap_device_role_on_sig(pkt->data, payload_len);
    } else {
        uint16_t payload_len = len - (sizeof(pkt->h4_hdr) + sizeof(pkt->acl_hdr) + sizeof(pkt->l2cap_hdr));
        bt_l2cap_device_role_on_channel_data(pkt->l2cap_hdr.cid, pkt->data, payload_len);
    }
}

void bt_device_role_poll(void) {
    /* Mostly event-driven; the one thing worth polling for is noticing once
     * both L2CAP channels finish configuring, since that completion can be
     * driven by either side's Configuration Response arriving last — there's
     * no single event that means "both are now open." */
    if (g_state == BT_DEV_STATE_LINK_ESTABLISHED && bt_l2cap_device_role_both_channels_open()) {
        /* Simplification: jumping straight to STREAMING rather than
         * threading through the separate BT_DEV_STATE_L2CAP_CTRL_OPEN /
         * BT_DEV_STATE_L2CAP_INTR_OPEN states in bt_device_role.h, since
         * bt_l2cap_device_role only currently reports both-open as a single
         * combined boolean. Revisit if per-channel state ever matters
         * (e.g. for debugging which channel is slow to configure). */
        g_state = BT_DEV_STATE_STREAMING;
    }
}

void bt_device_role_update_buttons(uint16_t relay_buttons, int8_t stick_x, int8_t stick_y) {
    if (g_state != BT_DEV_STATE_STREAMING) return;

    /* TODO: stick_x/stick_y aren't used yet — they matter for Nunchuk/
     * Classic Controller extension emulation, not the core buttons+accel
     * report being sent here. Wire them in once extension support (see
     * wiimote/wm_crypto.h's scope note) is added. */
    (void)stick_x;
    (void)stick_y;

    wm_report_btn_acc_t report;
    wm_reports_from_relay_buttons(relay_buttons, &report.buttons);
    /* Flat/placeholder accelerometer values — TODO_VERIFY: a real Wiimote's
     * idle/flat accelerometer reading is a specific per-unit calibrated
     * value, not necessarily the middle of the 0-255 range. Games that
     * check for plausible accelerometer behavior may notice this is static.
     * See wm_eeprom.c's calibration TODO — this is the same underlying gap. */
    report.accel_x = 0x80;
    report.accel_y = 0x80;
    report.accel_z = 0x80;

    uint8_t wire[1 + sizeof(report)];
    wire[0] = WM_IN_BTN_ACC;
    memcpy(&wire[1], &report, sizeof(report));

    bt_l2cap_device_role_send_interrupt(wire, sizeof(wire));
}
