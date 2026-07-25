/*
 * Shared wire format between Board A (controller host) and Board B
 * (Wii-facing device emulator). Keep this file byte-identical on both boards.
 *
 * This is intentionally simple: a fixed-size struct sent over UART with a
 * start byte and a trivial checksum, no fragmentation needed since it's tiny.
 */
#ifndef WII_RELAY_SHARED_PROTOCOL_H
#define WII_RELAY_SHARED_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define WII_RELAY_FRAME_START 0xA5
#define WII_RELAY_UART_BAUD   115200

/* Generic bitmask, mapped in board-a from whatever pad connects.
 * Board B maps these bits onto Wiimote/Classic Controller report bits —
 * see wiimote/wm_reports.h for that mapping table. */
typedef enum {
    WII_BTN_A      = 1 << 0,
    WII_BTN_B      = 1 << 1,
    WII_BTN_ONE    = 1 << 2,
    WII_BTN_TWO    = 1 << 3,
    WII_BTN_MINUS  = 1 << 4,
    WII_BTN_PLUS   = 1 << 5,
    WII_BTN_HOME   = 1 << 6,
    WII_BTN_UP     = 1 << 7,
    WII_BTN_DOWN   = 1 << 8,
    WII_BTN_LEFT   = 1 << 9,
    WII_BTN_RIGHT  = 1 << 10,
} wii_relay_button_bits_t;

#pragma pack(push, 1)
typedef struct {
    uint8_t  frame_start;   /* always WII_RELAY_FRAME_START */
    uint16_t buttons;       /* bitmask, see wii_relay_button_bits_t */
    int8_t   stick_x;       /* -127..127, for Nunchuk/Classic Controller emulation */
    int8_t   stick_y;
    uint8_t  seq;           /* incrementing counter, lets Board B detect drops */
    uint8_t  checksum;      /* XOR of all preceding bytes */
} wii_relay_frame_t;
#pragma pack(pop)

static inline uint8_t wii_relay_checksum(const wii_relay_frame_t *f) {
    const uint8_t *b = (const uint8_t *)f;
    uint8_t x = 0;
    for (size_t i = 0; i < sizeof(*f) - 1; i++) x ^= b[i];
    return x;
}

#endif
