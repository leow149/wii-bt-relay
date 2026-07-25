/*
 * Wiimote EXTENSION register "encryption" — NOT full-cipher implementation.
 *
 * Important scope note: this only matters if you emulate a Nunchuk/Classic
 * Controller extension living on your fake Wiimote's internal I2C bus. The
 * core button/accelerometer reports (0x30/0x31/etc, see wm_reports.h) that
 * make up the bulk of what a Wii reads from a Wiimote are NOT encrypted at
 * all — you can ignore this file entirely for a first-pass, buttons-only
 * implementation.
 *
 * For extensions: real Wiimote extensions support a simple XOR-based
 * obfuscation of their register data, but in practice almost every host-side
 * implementation (including BlueRetro — see the vendored bt_reference/wii.c,
 * functions writing 0x55 to register 0xF0 then 0x00 to register 0xFB) simply
 * DISABLES this obfuscation during the extension init handshake rather than
 * implementing the cipher. That's the approach taken here: wm_ext_init_seq()
 * below reproduces that same disable-encryption init sequence. The actual
 * cipher is intentionally NOT implemented — if you find a reason you need it
 * (e.g. a host that refuses cleartext extension data), that's new work, not
 * a gap being silently papered over here.
 */
#ifndef WM_CRYPTO_H
#define WM_CRYPTO_H

#include <stdint.h>

/* Sequence of (register_address, value) writes that disables extension
 * register encryption, reproduced from the pattern in bt_reference/wii.c. */
typedef struct {
    uint8_t reg_addr[3]; /* extension register space is addressed 0xA4,0x00,offset */
    uint8_t value;
} wm_ext_init_write_t;

extern const wm_ext_init_write_t wm_ext_disable_encryption_seq[];
extern const int wm_ext_disable_encryption_seq_len;

#endif
