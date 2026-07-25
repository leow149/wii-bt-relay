/*
 * See bt_sdp_device_role.h for scope/status notes.
 *
 * SDP PDU header: PDU ID (1 byte) + Transaction ID (2 bytes, big-endian) +
 * ParameterLength (2 bytes, big-endian) -- per the Bluetooth SDP spec, all
 * multi-byte SDP fields are big-endian (unlike most of the rest of this
 * repo's HCI/L2CAP code, which is little-endian per those specs -- easy
 * mixup, called out explicitly here).
 *
 * Data element header byte: top 5 bits = type descriptor, bottom 3 bits =
 * size descriptor. This file only needs a handful of element kinds:
 *   - Unsigned int, 4 bytes:  type 1, size index 2  -> header 0x0A
 *   - UUID, 2 bytes:          type 3, size index 1  -> header 0x19
 *   - Text string, N bytes:   type 4, size index 5  -> header 0x25, +1 byte length
 *   - Sequence, N bytes:      type 6, size index 5  -> header 0x35, +1 byte length
 * (size index 5/6/7 mean "an additional 8/16/32-bit length field follows" --
 * used here for anything whose length isn't fixed/implied by its type.)
 */
#include <string.h>
#include <stdio.h>

#include "bt_sdp_device_role.h"

#define SDP_PDU_SERVICE_SEARCH_ATTR_REQ  0x06
#define SDP_PDU_SERVICE_SEARCH_ATTR_RSP  0x07
#define SDP_PDU_ERROR_RESPONSE           0x01

#define SDP_UUID_L2CAP        0x0100
#define SDP_UUID_HIDP         0x0011
#define SDP_UUID_HID_SERVICE  0x1124
#define SDP_UUID_HID_PROFILE  0x1124

typedef struct {
    uint8_t *buf;
    uint16_t cap;
    uint16_t len;
} sdp_builder_t;

static void sb_init(sdp_builder_t *b, uint8_t *buf, uint16_t cap) {
    b->buf = buf; b->cap = cap; b->len = 0;
}

static void sb_u8(sdp_builder_t *b, uint8_t v) {
    if (b->len < b->cap) b->buf[b->len++] = v;
}
static void sb_u16(sdp_builder_t *b, uint16_t v) { sb_u8(b, (v >> 8) & 0xff); sb_u8(b, v & 0xff); }
static void sb_u32(sdp_builder_t *b, uint32_t v) {
    sb_u8(b, (v >> 24) & 0xff); sb_u8(b, (v >> 16) & 0xff);
    sb_u8(b, (v >> 8) & 0xff); sb_u8(b, v & 0xff);
}
static void sb_bytes(sdp_builder_t *b, const uint8_t *d, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) sb_u8(b, d[i]);
}

static void sb_elem_uint32(sdp_builder_t *b, uint32_t v) { sb_u8(b, 0x0A); sb_u32(b, v); }
static void sb_elem_uuid16(sdp_builder_t *b, uint16_t uuid) { sb_u8(b, 0x19); sb_u16(b, uuid); }
static void sb_elem_string(sdp_builder_t *b, const char *s) {
    uint8_t slen = (uint8_t)strlen(s);
    sb_u8(b, 0x25);
    sb_u8(b, slen);
    sb_bytes(b, (const uint8_t *)s, slen);
}

/* Sequence begin/end pair with backpatched length -- avoids needing a
 * second pass or dynamic allocation. Only supports sequences up to 255
 * bytes of content (8-bit length field); see the file header's scope note
 * on continuation-state / larger responses not being handled yet. */
typedef struct { uint16_t header_pos; } sdp_seq_mark_t;

static sdp_seq_mark_t sb_seq_begin(sdp_builder_t *b) {
    sdp_seq_mark_t m = { .header_pos = b->len };
    sb_u8(b, 0x35);
    sb_u8(b, 0); /* placeholder, backpatched in sb_seq_end */
    return m;
}
static void sb_seq_end(sdp_builder_t *b, sdp_seq_mark_t m) {
    uint16_t content_len = b->len - (m.header_pos + 2);
    if (content_len > 255) {
        printf("# sdp: sequence content %u bytes exceeds 8-bit length encoding "
               "(TODO: 16-bit sequence header / continuation state not implemented)\n",
               content_len);
        content_len = 255; /* avoid corrupting the rest of the packet, though
                             * the response is now truncated/wrong -- this is
                             * a hard failure case to fix if it's ever hit. */
    }
    if (m.header_pos + 1 < b->cap) b->buf[m.header_pos + 1] = (uint8_t)content_len;
}

static void sb_attr_id(sdp_builder_t *b, uint16_t attr_id) {
    /* Attribute IDs are always a 2-byte unsigned int on the wire: type 1,
     * size index 1 -> header 0x09. (Not to be confused with sb_elem_uint32,
     * used for 4-byte unsigned int values like the ServiceRecordHandle.) */
    sb_u8(b, 0x09);
    sb_u16(b, attr_id);
}

/* Builds the AttributeList content for the one Wiimote HID service record:
 * ServiceRecordHandle (0x0000), ServiceClassIDList (0x0001),
 * ProtocolDescriptorList (0x0004), BluetoothProfileDescriptorList (0x0009),
 * ServiceName (0x0100). Deliberately omits the HIDDescriptorList (0x0206)
 * attribute for now -- TODO: real Wiimotes include one, and some hosts may
 * expect it; adding it needs a real (or at least well-formed placeholder)
 * USB HID report descriptor blob, not written yet. */
static void build_wiimote_service_record(sdp_builder_t *b) {
    sdp_seq_mark_t record = sb_seq_begin(b);

    /* ServiceRecordHandle (0x0000) = arbitrary handle, matching the
     * general shape (not exact value) used by prior art for this record */
    sb_attr_id(b, 0x0000);
    sb_elem_uint32(b, 0x00010000);

    /* ServiceClassIDList (0x0001) = [ HIDService UUID ] */
    sb_attr_id(b, 0x0001);
    {
        sdp_seq_mark_t m = sb_seq_begin(b);
        sb_elem_uuid16(b, SDP_UUID_HID_SERVICE);
        sb_seq_end(b, m);
    }

    /* ProtocolDescriptorList (0x0004) = [ [L2CAP, PSM=0x11], [HIDP] ] */
    sb_attr_id(b, 0x0004);
    {
        sdp_seq_mark_t outer = sb_seq_begin(b);
        {
            sdp_seq_mark_t l2cap = sb_seq_begin(b);
            sb_elem_uuid16(b, SDP_UUID_L2CAP);
            sb_u8(b, 0x09); sb_u16(b, 0x0011); /* PSM, as a plain uint16 element */
            sb_seq_end(b, l2cap);
        }
        {
            sdp_seq_mark_t hidp = sb_seq_begin(b);
            sb_elem_uuid16(b, SDP_UUID_HIDP);
            sb_seq_end(b, hidp);
        }
        sb_seq_end(b, outer);
    }

    /* BluetoothProfileDescriptorList (0x0009) = [ [HIDProfile, v0x0100] ] */
    sb_attr_id(b, 0x0009);
    {
        sdp_seq_mark_t outer = sb_seq_begin(b);
        sdp_seq_mark_t inner = sb_seq_begin(b);
        sb_elem_uuid16(b, SDP_UUID_HID_PROFILE);
        sb_u8(b, 0x09); sb_u16(b, 0x0100); /* profile version 1.0 */
        sb_seq_end(b, inner);
        sb_seq_end(b, outer);
    }

    /* ServiceName (0x0100 -- primary-language base + 0x0000, the common
     * default when no LanguageBaseAttributeIDList is separately declared) */
    sb_attr_id(b, 0x0100);
    sb_elem_string(b, "Nintendo RVL-CNT-01");

    sb_seq_end(b, record);
}

uint16_t bt_sdp_device_role_handle_request(const uint8_t *req, uint16_t req_len,
                                            uint8_t *out_buf, uint16_t out_buf_size) {
    if (req_len < 5) return 0;

    uint8_t pdu_id = req[0];
    uint16_t transaction_id = (req[1] << 8) | req[2];
    /* req[3..4] = ParameterLength -- not validated here; TODO if strict
     * clients ever reject on a mismatch. */

    if (pdu_id != SDP_PDU_SERVICE_SEARCH_ATTR_REQ) {
        printf("# sdp: unhandled request PDU 0x%02x, not responding (TODO)\n", pdu_id);
        return 0;
    }

    sdp_builder_t body;
    uint8_t body_buf[300];
    sb_init(&body, body_buf, sizeof(body_buf));

    /* AttributeLists is itself a sequence containing one sequence per
     * matching service record -- we only ever have the one record. */
    sdp_seq_mark_t lists = sb_seq_begin(&body);
    build_wiimote_service_record(&body);
    sb_seq_end(&body, lists);

    sdp_builder_t out;
    sb_init(&out, out_buf, out_buf_size);
    sb_u8(&out, SDP_PDU_SERVICE_SEARCH_ATTR_RSP);
    sb_u16(&out, transaction_id);
    /* ParameterLength placeholder -- backpatched below once we know it */
    uint16_t param_len_pos = out.len;
    sb_u16(&out, 0);

    uint16_t param_start = out.len;
    sb_u16(&out, body.len);           /* AttributeListsByteCount */
    sb_bytes(&out, body_buf, body.len); /* the AttributeLists sequence itself */
    sb_u8(&out, 0x00);                /* ContinuationState: none (0-length) */
    uint16_t param_len = out.len - param_start;

    if (out.len > out_buf_size) {
        printf("# sdp: response %u bytes exceeds provided buffer %u\n", out.len, out_buf_size);
        return 0;
    }

    out.buf[param_len_pos] = (param_len >> 8) & 0xff;
    out.buf[param_len_pos + 1] = param_len & 0xff;

    if (out.len > 200) {
        /* Conservative heuristic threshold, not a real MTU check -- flags
         * that continuation-state fragmentation (not implemented, see
         * bt_sdp_device_role.h) may be needed if this grows further. */
        printf("# sdp: response is %u bytes; if the Wii's SDP client has a "
               "small incoming MTU this may need continuation-state support "
               "we haven't implemented\n", out.len);
    }

    return out.len;
}
