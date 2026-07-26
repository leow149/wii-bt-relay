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
 *
 * LOGGING: every cmd_* function and event branch below now prints a line.
 * Originally this file had none, which made a real hardware test opaque --
 * there was no way to tell from serial output whether the sequence was
 * progressing normally or stuck. Added after the first real device test.
 *
 * REAL BUG FOUND AND FIXED via that logging: every cmd_* function used to
 * call send_hci_cmd() and THEN set g_state to the "sent" value afterward.
 * A real hardware test showed bt_vhci_transport_send() delivers the
 * corresponding Command Complete event back into our own rx callback
 * synchronously/reentrantly -- before send_hci_cmd() itself returns. That
 * meant the event handler checked g_state *before* it had been updated,
 * saw the stale previous value, matched none of the dispatch branches, and
 * the whole bring-up sequence silently stalled after the very first command.
 * Fix: every cmd_* function now sets g_state BEFORE sending, not after.
 *
 * SECOND THING FOUND ON HARDWARE, PARTIALLY WRONG: with the above fixed,
 * bring-up completed all the way to "discoverable+connectable", and a
 * generic Bluetooth scan (another ESP32 running Bluepad32/BTstack) found
 * and connected to it fine -- but pressing the Wii's own SYNC button found
 * nothing at all. At the time, this looked like a Limited Discoverable
 * Mode / LIAC-vs-GIAC issue (a device in only general discoverable mode
 * won't answer a Limited Inquiry, per the Bluetooth GAP spec, and a
 * button-triggered pairing scan is the canonical LIAC use case) so
 * HCI_Write_Current_IAC_LAP was added below to register both. That part is
 * harmless and stays, but a REAL CAPTURE of an actual Wiimote (see finding
 * #3) shows the real Wiimote does NOT set the Limited Discoverable Mode
 * bit at all -- so this was very likely the wrong theory. Leaving the IAC
 * LAP command in place since responding to both GIAC and LIAC can't hurt,
 * but the "still nothing" report that followed this fix turned out to
 * have a different, better-evidenced cause: see finding #3.
 *
 * THIRD FINDING, FROM AN ACTUAL PACKET CAPTURE (not reasoning): the user
 * captured a real Wiimote's own over-the-air Class of Device using btmon
 * while pairing it to a PC. The real value is 0x000508 -- Major Device
 * Class Peripheral(HID) (same as ours), but Minor Device Class "Gamepad"
 * (bit pattern 0010), not "Joystick" (bit pattern 0001) like our old
 * 0x002504, and critically, NO Limited Discoverable Mode bit set at all
 * (confirming finding #2 above was chasing the wrong mechanism). The same
 * capture also caught Board B itself being correctly inquired by the PC,
 * reporting exactly the 0x002504 we'd set -- confirming our bring-up
 * sequence broadcasts precisely what we tell it to; the bug was in what we
 * were telling it, not whether it worked. Fixed cmd_write_class_of_device()
 * below to use the real, captured 0x000508 instead of the previous value
 * (which came from a working reference project, but apparently didn't
 * match a genuine retail Wiimote's minor device class closely enough).
 */
#include <string.h>
#include <stdio.h>

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

/* Not present in the vendored zephyr_hci_defs.h -- see the file header note
 * above for why. Real HCI opcode/parameter layout per the Bluetooth Core
 * Spec's Controller & Baseband command group, not guessed:
 *   OGF = 0x03 (Baseband), OCF = 0x3A -> opcode 0x0C3A
 *   Params: Num_Current_IAC (1 octet), then that many 3-octet LAP values.
 * GIAC (General Inquiry Access Code) = 0x9E8B33.
 * LIAC (Limited Inquiry Access Code) = 0x9E8B00.
 * Both registered together so we keep responding to ordinary/general scans
 * (like Board A's own Bluepad32/BTstack, or a phone) as well as any Limited
 * Inquiry a host might use -- see the file header's finding #2/#3 for why
 * this turned out not to be the actual mechanism a real Wiimote relies on,
 * but it's kept since it's harmless. */
#define BT_HCI_OP_WRITE_CURRENT_IAC_LAP BT_OP(BT_OGF_BASEBAND, 0x003a)
struct bt_hci_cp_write_current_iac_lap {
    uint8_t num_current_iac;
    uint8_t iac_lap[2][3]; /* [0] = GIAC, [1] = LIAC -- see cmd_write_current_iac_lap() */
} __packed;

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
    printf("# bt_device_role: send_hci_cmd opcode=0x%04x len=%d\n", opcode, param_len);
    bt_vhci_transport_send(pkt, 4 + param_len);
}

static void cmd_reset(void) {
    printf("# bt_device_role: cmd_reset\n");
    /* State set BEFORE send -- see file header note on why order matters. */
    g_state = BT_DEV_STATE_RESET_SENT;
    send_hci_cmd(BT_HCI_OP_RESET, NULL, 0);
}

static void cmd_write_local_name(void) {
    printf("# bt_device_role: cmd_write_local_name -> \"%s\"\n", WM_DEVICE_NAME);
    struct bt_hci_cp_write_local_name cp;
    memset(&cp, 0, sizeof(cp));
    strncpy((char *)cp.local_name, WM_DEVICE_NAME, sizeof(cp.local_name) - 1);
    g_state = BT_DEV_STATE_NAME_SET;
    send_hci_cmd(BT_HCI_OP_WRITE_LOCAL_NAME, &cp, sizeof(cp));
}

static void cmd_write_class_of_device(void) {
    printf("# bt_device_role: cmd_write_class_of_device -> 0x000508\n");
    struct bt_hci_cp_write_class_of_device cp;
    /* REAL, CAPTURED value -- see file header finding #3. A real Wiimote
     * was actually sniffed over the air via btmon during a PC pairing
     * session, and its true Class of Device is 0x000508: Major Device
     * Class Peripheral(HID) (0x05, same as before), Minor Device Class
     * Gamepad (bit pattern 0010), and no Limited Discoverable Mode bit.
     * This REPLACES the previous 0x002504 value (Minor Device Class
     * Joystick, bit pattern 0001, with the Limited Discoverable bit set),
     * which came from a working reference project but apparently didn't
     * match a genuine retail Wiimote's advertised minor device class.
     * Wire bytes are little-endian per the 24-bit CoD field: byte0 (LSB)
     * = 0x08, byte1 = 0x05, byte2 (MSB) = 0x00. */
    cp.dev_class[0] = 0x08;
    cp.dev_class[1] = 0x05;
    cp.dev_class[2] = 0x00;
    g_state = BT_DEV_STATE_COD_SET;
    send_hci_cmd(BT_HCI_OP_WRITE_CLASS_OF_DEVICE, &cp, sizeof(cp));
}

static void cmd_write_scan_enable(void) {
    printf("# bt_device_role: cmd_write_scan_enable -> discoverable+connectable\n");
    struct bt_hci_cp_write_scan_enable cp;
    cp.scan_enable = 0x03; /* bit0 = inquiry scan, bit1 = page scan: both on
                             * -> discoverable AND connectable */
    g_state = BT_DEV_STATE_SCAN_ENABLED;
    send_hci_cmd(BT_HCI_OP_WRITE_SCAN_ENABLE, &cp, sizeof(cp));
}

static void cmd_write_current_iac_lap(void) {
    printf("# bt_device_role: cmd_write_current_iac_lap -> GIAC+LIAC\n");
    /* See the file header note above (findings #2/#3): a real Wiimote does
     * NOT rely on Limited Discoverable Mode, so this almost certainly
     * wasn't the fix the Wii's sync button needed -- kept anyway since
     * responding to both GIAC and LIAC is harmless. */
    struct bt_hci_cp_write_current_iac_lap cp;
    cp.num_current_iac = 2;
    cp.iac_lap[0][0] = 0x33; cp.iac_lap[0][1] = 0x8B; cp.iac_lap[0][2] = 0x9E; /* GIAC 0x9E8B33 */
    cp.iac_lap[1][0] = 0x00; cp.iac_lap[1][1] = 0x8B; cp.iac_lap[1][2] = 0x9E; /* LIAC 0x9E8B00 */
    g_state = BT_DEV_STATE_IAC_LAP_SET;
    send_hci_cmd(BT_HCI_OP_WRITE_CURRENT_IAC_LAP, &cp, sizeof(cp));
}

static void cmd_write_ssp_mode_disable(void) {
    printf("# bt_device_role: cmd_write_ssp_mode_disable\n");
    /* The Wii only speaks legacy PIN-based pairing. rnconrad's adapter.c
     * explicitly disables Simple Secure Pairing mode before anything else
     * (set_up_simple_pairing_mode(dd) writing 0) — without this, a modern
     * Bluetooth controller may default to SSP and never get as far as our
     * PIN reply logic at all. */
    struct bt_hci_cp_write_ssp_mode cp;
    cp.mode = 0x00;
    g_state = BT_DEV_STATE_SSP_DISABLED;
    send_hci_cmd(BT_HCI_OP_WRITE_SSP_MODE, &cp, sizeof(cp));
}

static void cmd_accept_connection_request(const bt_addr_t *bdaddr) {
    printf("# bt_device_role: cmd_accept_connection_request from %02X:%02X:%02X:%02X:%02X:%02X\n",
           bdaddr->val[5], bdaddr->val[4], bdaddr->val[3], bdaddr->val[2], bdaddr->val[1], bdaddr->val[0]);
    struct bt_hci_cp_accept_conn_req cp;
    cp.bdaddr = *bdaddr;
    cp.role = 0x01; /* 0x01 = remain SLAVE. This is the key flip versus
                      * BlueRetro's host-role code, which hardcodes 0x00
                      * (become master) at the equivalent call site in
                      * ../bt_reference/hci.c. */
    g_state = BT_DEV_STATE_ACCEPTING_CONN;
    send_hci_cmd(BT_HCI_OP_ACCEPT_CONN_REQ, &cp, sizeof(cp));
}

static void cmd_pin_code_reply(const bt_addr_t *bdaddr) {
    printf("# bt_device_role: cmd_pin_code_reply using bdaddr %02X:%02X:%02X:%02X:%02X:%02X (reversed) as PIN\n",
           bdaddr->val[5], bdaddr->val[4], bdaddr->val[3], bdaddr->val[2], bdaddr->val[1], bdaddr->val[0]);
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
    g_state = BT_DEV_STATE_PIN_EXCHANGE;
    send_hci_cmd(BT_HCI_OP_PIN_CODE_REPLY, &cp, sizeof(cp));
}

void bt_device_role_init(void) {
    printf("# bt_device_role: init, starting HCI bring-up sequence\n");
    g_state = BT_DEV_STATE_INIT;
    /* bt_vhci_transport_init() itself calls back into us via
     * bt_device_role_on_hci_event() once packets start arriving; the
     * Reset command below is sent only after the transport is up. */
    cmd_reset();
    /* Remaining bring-up (name/CoD/scan enable/IAC LAP/SSP) is driven from
     * the Command Complete event for Reset in bt_device_role_on_hci_event()
     * below, since each HCI command must wait for its predecessor's
     * completion event before the controller will accept the next one. */
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
            printf("# bt_device_role: EVT_CMD_COMPLETE (opcode=0x%04x, state was %d)\n", cc->opcode, g_state);
            if (g_state == BT_DEV_STATE_RESET_SENT) cmd_write_local_name();
            else if (g_state == BT_DEV_STATE_NAME_SET) cmd_write_class_of_device();
            else if (g_state == BT_DEV_STATE_COD_SET) cmd_write_scan_enable();
            else if (g_state == BT_DEV_STATE_SCAN_ENABLED) cmd_write_current_iac_lap();
            else if (g_state == BT_DEV_STATE_IAC_LAP_SET) cmd_write_ssp_mode_disable();
            else if (g_state == BT_DEV_STATE_SSP_DISABLED) {
                g_state = BT_DEV_STATE_WAIT_CONN_REQUEST;
                printf("# bt_device_role: bring-up complete, now discoverable+connectable as \"%s\"\n", WM_DEVICE_NAME);
            }
            break;
        }

        case BT_HCI_EVT_CONN_REQUEST: {
            struct bt_hci_evt_conn_request *cr = (struct bt_hci_evt_conn_request *)pkt->data;
            printf("# bt_device_role: EVT_CONN_REQUEST\n");
            g_peer_addr = cr->bdaddr;
            cmd_accept_connection_request(&g_peer_addr);
            break;
        }

        case BT_HCI_EVT_PIN_CODE_REQ: {
            struct bt_hci_evt_pin_code_req *pr = (struct bt_hci_evt_pin_code_req *)pkt->data;
            printf("# bt_device_role: EVT_PIN_CODE_REQ\n");
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
            printf("# bt_device_role: EVT_CONN_COMPLETE status=0x%02x handle=0x%04x\n", cc->status, cc->handle);
            if (cc->status == 0x00) {
                g_acl_handle = cc->handle;
                g_state = BT_DEV_STATE_LINK_ESTABLISHED;
                bt_l2cap_device_role_init(g_acl_handle);
            } else {
                printf("# bt_device_role: connection failed, status=0x%02x -> ERROR state\n", cc->status);
                g_state = BT_DEV_STATE_ERROR;
            }
            break;
        }

        default:
            printf("# bt_device_role: unhandled HCI event 0x%02x\n", pkt->evt_hdr.evt);
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
        printf("# bt_device_role: both L2CAP channels open -> STREAMING\n");
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
