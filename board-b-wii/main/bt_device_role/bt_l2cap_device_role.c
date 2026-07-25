/*
 * See bt_l2cap_device_role.h for scope/status notes.
 */
#include <string.h>
#include <stdio.h>

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

#include "bt_l2cap_device_role.h"
#include "bt_device_role.h" /* for WM_L2CAP_PSM_CONTROL / WM_L2CAP_PSM_INTERRUPT */
#include "bt_sdp_device_role.h"
#include "zephyr_hci_defs.h"
#include "zephyr_l2cap_defs.h"
#include "bt_vhci_transport.h"

#define SDP_PSM 0x0001

/* Locally-allocated channel IDs for our three channels. 0x0040 is the start
 * of the dynamically-allocated CID range per the Bluetooth Core
 * Specification (a fixed public convention, not BlueRetro- or
 * Wiimote-specific) — picking arbitrary values in that range is standard
 * practice. */
#define WM_LOCAL_CID_SDP  0x003F
#define WM_LOCAL_CID_CTRL 0x0040
#define WM_LOCAL_CID_INTR 0x0041

typedef struct {
    uint16_t psm;
    uint16_t local_cid;
    uint16_t remote_cid;   /* their scid, learned from their Connection Request */
    bool conn_rsp_sent;
    bool our_conf_req_sent;
    bool their_conf_req_acked; /* we've sent them a Configuration Response */
    bool our_conf_req_acked;   /* they've sent us a Configuration Response */
} l2cap_chan_state_t;

static l2cap_chan_state_t g_sdp  = { .psm = SDP_PSM, .local_cid = WM_LOCAL_CID_SDP };
static l2cap_chan_state_t g_ctrl = { .psm = WM_L2CAP_PSM_CONTROL, .local_cid = WM_LOCAL_CID_CTRL };
static l2cap_chan_state_t g_intr = { .psm = WM_L2CAP_PSM_INTERRUPT, .local_cid = WM_LOCAL_CID_INTR };
static uint16_t g_handle = 0;
static uint8_t g_tx_ident = 0;

static uint8_t next_ident(void) {
    g_tx_ident++;
    if (g_tx_ident == 0) g_tx_ident = 1; /* ident 0 is reserved, per spec */
    return g_tx_ident;
}

void bt_l2cap_device_role_init(uint16_t acl_handle) {
    g_handle = acl_handle;
    memset(&g_sdp, 0, sizeof(g_sdp));
    memset(&g_ctrl, 0, sizeof(g_ctrl));
    memset(&g_intr, 0, sizeof(g_intr));
    g_sdp.psm = SDP_PSM;
    g_sdp.local_cid = WM_LOCAL_CID_SDP;
    g_ctrl.psm = WM_L2CAP_PSM_CONTROL;
    g_ctrl.local_cid = WM_LOCAL_CID_CTRL;
    g_intr.psm = WM_L2CAP_PSM_INTERRUPT;
    g_intr.local_cid = WM_LOCAL_CID_INTR;
}

/* -- Low-level ACL/L2CAP signaling packet send -----------------------------
 * Mirrors the byte layout BlueRetro's bt_l2cap_cmd() builds (H4 ACL header +
 * ACL header + L2CAP header + signaling header + payload), but written
 * standalone against our own bt_vhci_transport_send() rather than
 * BlueRetro's internal TX queue, since we're a single peripheral link
 * rather than a multi-device host. */
static void send_l2cap_sig(uint8_t code, uint8_t ident, const void *params, uint16_t param_len) {
    uint8_t pkt[4 + 4 + 4 + 64]; /* H4(1) + acl_hdr(4) + l2cap_hdr(4) + sig_hdr(4) + payload */
    uint16_t l2cap_payload_len = sizeof(struct bt_l2cap_sig_hdr) + param_len;
    uint16_t acl_data_len = sizeof(struct bt_l2cap_hdr) + l2cap_payload_len;

    struct bt_hci_acl_hdr *acl_hdr;
    struct bt_l2cap_hdr *l2cap_hdr;
    struct bt_l2cap_sig_hdr *sig_hdr;
    uint8_t *body;

    pkt[0] = BT_HCI_H4_TYPE_ACL;

    acl_hdr = (struct bt_hci_acl_hdr *)&pkt[1];
    acl_hdr->handle = bt_acl_handle_pack(g_handle, 0x2); /* 0x2 = Packet_Boundary_Flag: complete L2CAP PDU, first fragment */
    acl_hdr->len = acl_data_len;

    l2cap_hdr = (struct bt_l2cap_hdr *)&pkt[1 + sizeof(*acl_hdr)];
    l2cap_hdr->len = l2cap_payload_len;
    l2cap_hdr->cid = BT_L2CAP_CID_BR_SIG;

    sig_hdr = (struct bt_l2cap_sig_hdr *)&pkt[1 + sizeof(*acl_hdr) + sizeof(*l2cap_hdr)];
    sig_hdr->code = code;
    sig_hdr->ident = ident;
    sig_hdr->len = param_len;

    body = &pkt[1 + sizeof(*acl_hdr) + sizeof(*l2cap_hdr) + sizeof(*sig_hdr)];
    if (param_len) memcpy(body, params, param_len);

    bt_vhci_transport_send(pkt, 1 + sizeof(*acl_hdr) + sizeof(*l2cap_hdr) + sizeof(*sig_hdr) + param_len);
}

static void send_conn_rsp(uint8_t ident, l2cap_chan_state_t *chan) {
    struct bt_l2cap_conn_rsp rsp;
    rsp.dcid = chan->local_cid;
    rsp.scid = chan->remote_cid;
    rsp.result = BT_L2CAP_BR_SUCCESS;
    rsp.status = BT_L2CAP_CS_NO_INFO;
    send_l2cap_sig(BT_L2CAP_CONN_RSP, ident, &rsp, sizeof(rsp));
    chan->conn_rsp_sent = true;
}

static void send_conf_req(l2cap_chan_state_t *chan) {
    /* Config Request payload: dcid (their channel id, from our perspective
     * the "destination" we're configuring) + flags, then a single MTU
     * option, mirroring BlueRetro's bt_l2cap_cmd_conf_req exactly. */
    uint8_t buf[sizeof(struct bt_l2cap_conf_req) + sizeof(struct bt_l2cap_conf_opt) + sizeof(uint16_t)];
    struct bt_l2cap_conf_req *req = (struct bt_l2cap_conf_req *)buf;
    struct bt_l2cap_conf_opt *opt = (struct bt_l2cap_conf_opt *)req->data;
    uint16_t mtu = 672; /* matches BlueRetro's BT_L2CAP_DEFAULT_MTU */

    req->dcid = chan->remote_cid;
    req->flags = 0x0000;
    opt->type = BT_L2CAP_CONF_OPT_MTU;
    opt->len = sizeof(uint16_t);
    memcpy(opt->data, &mtu, sizeof(mtu));

    uint16_t total = (sizeof(*req) - sizeof(req->data)) + (sizeof(*opt) - sizeof(opt->data)) + sizeof(mtu);
    send_l2cap_sig(BT_L2CAP_CONF_REQ, next_ident(), buf, total);
    chan->our_conf_req_sent = true;
}

static void send_conf_rsp(uint8_t ident, l2cap_chan_state_t *chan) {
    uint8_t buf[sizeof(struct bt_l2cap_conf_rsp) + sizeof(struct bt_l2cap_conf_opt) + sizeof(uint16_t)];
    struct bt_l2cap_conf_rsp *rsp = (struct bt_l2cap_conf_rsp *)buf;
    struct bt_l2cap_conf_opt *opt = (struct bt_l2cap_conf_opt *)rsp->data;
    uint16_t mtu = 672;

    rsp->scid = chan->remote_cid;
    rsp->flags = 0x0000;
    rsp->result = BT_L2CAP_CONF_SUCCESS;
    opt->type = BT_L2CAP_CONF_OPT_MTU;
    opt->len = sizeof(uint16_t);
    memcpy(opt->data, &mtu, sizeof(mtu));

    uint16_t total = (sizeof(*rsp) - sizeof(rsp->data)) + (sizeof(*opt) - sizeof(opt->data)) + sizeof(mtu);
    send_l2cap_sig(BT_L2CAP_CONF_RSP, ident, buf, total);
    chan->their_conf_req_acked = true;
}

static l2cap_chan_state_t *chan_for_psm(uint16_t psm) {
    if (psm == g_sdp.psm) return &g_sdp;
    if (psm == g_ctrl.psm) return &g_ctrl;
    if (psm == g_intr.psm) return &g_intr;
    return NULL;
}

static l2cap_chan_state_t *chan_for_local_cid(uint16_t cid) {
    if (cid == g_sdp.local_cid) return &g_sdp;
    if (cid == g_ctrl.local_cid) return &g_ctrl;
    if (cid == g_intr.local_cid) return &g_intr;
    return NULL;
}

void bt_l2cap_device_role_on_sig(const uint8_t *l2cap_payload, uint16_t len) {
    if (len < sizeof(struct bt_l2cap_sig_hdr)) return;
    const struct bt_l2cap_sig_hdr *sig = (const struct bt_l2cap_sig_hdr *)l2cap_payload;
    const uint8_t *params = l2cap_payload + sizeof(*sig);
    uint16_t param_len = len - sizeof(*sig);

    switch (sig->code) {
        case BT_L2CAP_CONN_REQ: {
            if (param_len < sizeof(struct bt_l2cap_conn_req)) return;
            const struct bt_l2cap_conn_req *req = (const struct bt_l2cap_conn_req *)params;
            l2cap_chan_state_t *chan = chan_for_psm(req->psm);
            if (!chan) {
                /* TODO: send a Connection Response with
                 * BT_L2CAP_BR_ERR_PSM_NOT_SUPP for PSMs we don't handle,
                 * rather than silently ignoring — a real Wii may retry or
                 * stall waiting for a response. Not implemented yet. */
                printf("# l2cap: conn req for unknown PSM 0x%04x, ignoring (TODO)\n", req->psm);
                return;
            }
            chan->remote_cid = req->scid;
            send_conn_rsp(sig->ident, chan);
            /* Proactively send our own Configuration Request rather than
             * waiting — mirrors the common real-world ordering where both
             * sides configure without strictly waiting on each other,
             * though the L2CAP spec doesn't require this particular
             * ordering. TODO_VERIFY this matches what a real Wii expects
             * timing-wise. */
            send_conf_req(chan);
            break;
        }

        case BT_L2CAP_CONF_REQ: {
            if (param_len < sizeof(struct bt_l2cap_conf_req)) return;
            const struct bt_l2cap_conf_req *req = (const struct bt_l2cap_conf_req *)params;
            l2cap_chan_state_t *chan = chan_for_local_cid(req->dcid);
            if (!chan) {
                printf("# l2cap: conf req for unknown local cid 0x%04x\n", req->dcid);
                return;
            }
            /* Not inspecting their requested options (e.g. their MTU) —
             * we always reply accepting with our own fixed default MTU.
             * TODO: honor their requested MTU/options if a real Wii sends
             * ones we should respect instead of just ACKing blindly. */
            send_conf_rsp(sig->ident, chan);
            break;
        }

        case BT_L2CAP_CONF_RSP: {
            if (param_len < sizeof(struct bt_l2cap_conf_rsp)) return;
            const struct bt_l2cap_conf_rsp *rsp = (const struct bt_l2cap_conf_rsp *)params;
            l2cap_chan_state_t *chan = chan_for_local_cid(rsp->scid);
            if (!chan) {
                printf("# l2cap: conf rsp for unknown local cid 0x%04x\n", rsp->scid);
                return;
            }
            if (rsp->result == BT_L2CAP_CONF_SUCCESS) {
                chan->our_conf_req_acked = true;
            }
            break;
        }

        default:
            /* Disconnect Request/Response and others not yet handled —
             * see bt_l2cap_device_role.h scope note. */
            break;
    }
}

bool bt_l2cap_device_role_both_channels_open(void) {
    bool ctrl_open = g_ctrl.conn_rsp_sent && g_ctrl.their_conf_req_acked && g_ctrl.our_conf_req_acked;
    bool intr_open = g_intr.conn_rsp_sent && g_intr.their_conf_req_acked && g_intr.our_conf_req_acked;
    return ctrl_open && intr_open;
}

static void bt_l2cap_send_data_on_cid(uint16_t remote_cid, const uint8_t *data, uint16_t len);

void bt_l2cap_device_role_send_interrupt(const uint8_t *data, uint16_t len) {
    if (!bt_l2cap_device_role_both_channels_open()) return;
    bt_l2cap_send_data_on_cid(g_intr.remote_cid, data, len);
}

/* Generic "send data on an already-open channel" -- used both by
 * send_interrupt above and by the SDP responder below. Channel data
 * packets are addressed to the REMOTE's cid for that channel, unlike
 * signaling which always uses cid 0x0001. */
static void bt_l2cap_send_data_on_cid(uint16_t remote_cid, const uint8_t *data, uint16_t len) {
    uint8_t pkt[4 + 4 + 300];
    struct bt_hci_acl_hdr *acl_hdr;
    struct bt_l2cap_hdr *l2cap_hdr;

    if (len > sizeof(pkt) - 5) {
        printf("# l2cap: payload too large for this send buffer, dropping\n");
        return;
    }

    pkt[0] = BT_HCI_H4_TYPE_ACL;
    acl_hdr = (struct bt_hci_acl_hdr *)&pkt[1];
    acl_hdr->handle = bt_acl_handle_pack(g_handle, 0x2);
    acl_hdr->len = sizeof(struct bt_l2cap_hdr) + len;

    l2cap_hdr = (struct bt_l2cap_hdr *)&pkt[1 + sizeof(*acl_hdr)];
    l2cap_hdr->len = len;
    l2cap_hdr->cid = remote_cid;

    memcpy(&pkt[1 + sizeof(*acl_hdr) + sizeof(*l2cap_hdr)], data, len);
    bt_vhci_transport_send(pkt, 1 + sizeof(*acl_hdr) + sizeof(*l2cap_hdr) + len);
}

bool bt_l2cap_device_role_on_channel_data(uint16_t cid, const uint8_t *data, uint16_t len) {
    l2cap_chan_state_t *chan = chan_for_local_cid(cid);
    if (!chan) return false;

    if (chan == &g_sdp) {
        uint8_t rsp[300];
        uint16_t rsp_len = bt_sdp_device_role_handle_request(data, len, rsp, sizeof(rsp));
        if (rsp_len > 0) {
            bt_l2cap_send_data_on_cid(g_sdp.remote_cid, rsp, rsp_len);
        }
        return true;
    }

    /* Control/interrupt channel data would be the Wii's output reports
     * (LED, rumble) -- not handled yet, see docs/STATUS.md. */
    return true;
}
