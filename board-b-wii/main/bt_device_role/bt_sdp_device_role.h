/*
 * EXPERIMENTAL — see docs/STATUS.md.
 *
 * Minimal SDP (Service Discovery Protocol) responder for the Wiimote's HID
 * service record. rnconrad/WiimoteEmulator's sdp.c notes: "The Wii simply
 * checks for the Wiimote service, verifies its attributes, and moves on" —
 * i.e. real Wiimotes don't run a full SDP server either, just enough to
 * answer this one browse.
 *
 * Deliberately NOT a byte-for-byte copy of any existing implementation's
 * SDP response payload. This builds the response fresh from the public SDP
 * wire format spec (data element type/size encoding, PDU header layout),
 * targeting the same essential attributes a Bluetooth HID device needs:
 * ServiceClassIDList, ProtocolDescriptorList, BluetoothProfileDescriptorList,
 * ServiceName. See docs/STATUS.md for what's verified vs assumed here.
 *
 * NOT implemented: SDP continuation-state fragmentation for responses
 * exceeding one L2CAP payload. Current response size is checked against a
 * conservative limit and a warning logged if it would need this — see the
 * TODO in bt_sdp_device_role.c.
 */
#ifndef BT_SDP_DEVICE_ROLE_H
#define BT_SDP_DEVICE_ROLE_H

#include <stdint.h>

/* Handles one incoming SDP request PDU (as received on the SDP L2CAP data
 * channel, PSM 0x0001) and writes a response PDU into out_buf.
 * Returns the response length, or 0 if the request type isn't handled
 * (only SDP_ServiceSearchAttributeRequest, PDU ID 0x06, is implemented --
 * that's the combined search+attribute-fetch request most SDP clients use
 * for a single, already-known-service-class lookup like this one). */
uint16_t bt_sdp_device_role_handle_request(const uint8_t *req, uint16_t req_len,
                                            uint8_t *out_buf, uint16_t out_buf_size);

#endif
