/*
 * See wm_reports.h for status notes on how well-verified this mapping is.
 */
#include <string.h>
#include "wm_reports.h"

/* Mirrors wii_relay_button_bits_t from shared_protocol.h without a hard
 * dependency on Board A's include path — keep these two files' bit meanings
 * in sync manually, since they live in different build trees. */
enum {
    RELAY_BTN_A      = 1 << 0,
    RELAY_BTN_B      = 1 << 1,
    RELAY_BTN_ONE    = 1 << 2,
    RELAY_BTN_TWO    = 1 << 3,
    RELAY_BTN_MINUS  = 1 << 4,
    RELAY_BTN_PLUS   = 1 << 5,
    RELAY_BTN_HOME   = 1 << 6,
    RELAY_BTN_UP     = 1 << 7,
    RELAY_BTN_DOWN   = 1 << 8,
    RELAY_BTN_LEFT   = 1 << 9,
    RELAY_BTN_RIGHT  = 1 << 10,
};

void wm_reports_from_relay_buttons(uint16_t relay_buttons, wm_core_buttons_t *out) {
    memset(out, 0, sizeof(*out));

    out->a          = (relay_buttons & RELAY_BTN_A)     ? 1 : 0;
    out->b           = (relay_buttons & RELAY_BTN_B)     ? 1 : 0;
    out->one         = (relay_buttons & RELAY_BTN_ONE)   ? 1 : 0;
    out->two         = (relay_buttons & RELAY_BTN_TWO)   ? 1 : 0;
    out->minus       = (relay_buttons & RELAY_BTN_MINUS) ? 1 : 0;
    out->plus        = (relay_buttons & RELAY_BTN_PLUS)  ? 1 : 0;
    out->home        = (relay_buttons & RELAY_BTN_HOME)  ? 1 : 0;
    out->dpad_up     = (relay_buttons & RELAY_BTN_UP)    ? 1 : 0;
    out->dpad_down   = (relay_buttons & RELAY_BTN_DOWN)  ? 1 : 0;
    out->dpad_left   = (relay_buttons & RELAY_BTN_LEFT)  ? 1 : 0;
    out->dpad_right  = (relay_buttons & RELAY_BTN_RIGHT) ? 1 : 0;
}
