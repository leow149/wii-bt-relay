/*
 * EXPERIMENTAL / UNVERIFIED — see docs/STATUS.md.
 *
 * Accepts the three L2CAP channels a Wii opens toward a Wiimote: SDP
 * (PSM 0x01, used for the service browse before the real channels open),
 * control (PSM 0x11), and interrupt (PSM 0x13), once the ACL link is up.
 * Struct layouts and result/option codes come from
 * ../bt_reference/zephyr_l2cap_defs.h (real, verified — see NOTICE). The
 * acceptor-side sequencing itself (which messages to send in which order)
 * follows the standard L2CAP channel establishment procedure and
 * BlueRetro's own conn_rsp/conf_req/conf_rsp builder functions as a
 * reference for exact field usage, adapted here for a single peripheral
 * link instead of BlueRetro's multi-device host model.
 */
#ifndef BT_L2CAP_DEVICE_ROLE_H
#define BT_L2CAP_DEVICE_ROLE_H

#include <stdint.h>
#include <stdbool.h>

/* Call once the ACL link is up (BT_DEV_STATE_LINK_ESTABLISHED) to reset
 * per-channel state before the Wii starts opening L2CAP channels. */
void bt_l2cap_device_role_init(uint16_t acl_handle);

/* Feed this with the L2CAP payload of every ACL packet received on the
 * signaling channel (cid 0x0001) — see bt_device_role.c's ACL dispatch. */
void bt_l2cap_device_role_on_sig(const uint8_t *l2cap_payload, uint16_t len);

/* Feed this with the L2CAP payload of any ACL packet whose cid matches one
 * of OUR local channel IDs (i.e. not the signaling channel) — currently
 * only meaningful for the SDP channel, which routes to
 * bt_sdp_device_role_handle_request() and sends the reply back on the same
 * channel. Control/interrupt channel data (output reports) isn't handled
 * yet — see docs/STATUS.md. Returns true if cid was recognized as one of
 * ours (regardless of whether anything was actually done with the data). */
bool bt_l2cap_device_role_on_channel_data(uint16_t cid, const uint8_t *data, uint16_t len);

/* True once both the control and interrupt channels have completed
 * configuration in both directions — bt_device_role.c polls this to know
 * when to move to BT_DEV_STATE_STREAMING. Independent of the SDP channel,
 * which typically opens, gets used, and closes earlier in the sequence. */
bool bt_l2cap_device_role_both_channels_open(void);

/* Send a Wiimote input report (see wiimote/wm_reports.h) over the
 * already-open interrupt channel. No-op / logs an error if the channel
 * isn't open yet. */
void bt_l2cap_device_role_send_interrupt(const uint8_t *data, uint16_t len);

#endif
