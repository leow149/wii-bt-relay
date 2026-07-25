/*
 * Receives wii_relay_frame_t frames from Board A over UART and feeds them
 * into bt_device_role_update_buttons(). This side is much lower-risk than
 * bt_device_role.c — it's just UART framing + a checksum check.
 *
 * NOTE: includes shared_protocol.h from board-a-controller's include path
 * (../../board-a-controller/include/) rather than duplicating it, so the two
 * boards can't silently drift out of sync on the wire format. Adjust the
 * include path in main/CMakeLists.txt if you restructure the repo.
 */
#include <string.h>
#include <driver/uart.h>
#include "shared_protocol.h"
#include "bt_device_role.h"

#define BOARD_A_UART_PORT UART_NUM_1
#define BOARD_A_UART_RX_PIN 16
#define BOARD_A_UART_TX_PIN 17 /* unused on this side, but uart driver wants a value */

static uint8_t g_last_seq = 0;
static int g_have_last_seq = 0;

void uart_bridge_init(void) {
    uart_config_t cfg = {
        .baud_rate = WII_RELAY_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(BOARD_A_UART_PORT, &cfg);
    uart_set_pin(BOARD_A_UART_PORT, BOARD_A_UART_TX_PIN, BOARD_A_UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(BOARD_A_UART_PORT, 256, 0, 0, NULL, 0);
}

/* Call periodically from the main loop (see main.c). Non-blocking: reads
 * whatever's available, processes zero or more complete frames. */
void uart_bridge_poll(void) {
    static uint8_t buf[sizeof(wii_relay_frame_t)];
    static size_t have = 0;

    int avail = uart_read_bytes(BOARD_A_UART_PORT, buf + have,
                                 sizeof(buf) - have, 0 /* no wait */);
    if (avail <= 0) return;
    have += avail;

    if (have < sizeof(wii_relay_frame_t)) return;

    wii_relay_frame_t *f = (wii_relay_frame_t *)buf;
    if (f->frame_start != WII_RELAY_FRAME_START) {
        /* Not aligned — drop one byte and resync on the next poll rather
         * than silently misinterpreting a shifted frame. */
        memmove(buf, buf + 1, --have);
        return;
    }

    if (wii_relay_checksum(f) != f->checksum) {
        /* Bad frame; drop it and resync from the next byte. */
        memmove(buf, buf + 1, --have);
        return;
    }

    if (!g_have_last_seq || f->seq != g_last_seq) {
        g_last_seq = f->seq;
        g_have_last_seq = 1;
        bt_device_role_update_buttons(f->buttons, f->stick_x, f->stick_y);
    }
    /* else: duplicate/retransmit of a frame we already processed, ignore */

    have = 0; /* consumed */
}
