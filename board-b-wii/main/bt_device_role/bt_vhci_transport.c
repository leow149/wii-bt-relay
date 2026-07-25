/*
 * VHCI transport wiring for the device-role stack.
 *
 * Unlike the rest of bt_device_role/, this file's init sequence and callback
 * registration pattern is copied directly from the real, working pattern in
 * ../bt_reference/host.c (BlueRetro's host-role bring-up) — esp_bt_controller_init,
 * esp_bt_controller_enable(), esp_vhci_host_register_callback with a
 * {tx_ready, rx_pkt} callback struct are all real ESP-IDF esp_bt.h API,
 * confirmed against working code rather than recalled from memory. This part
 * is genuinely low-risk — it's the same bring-up any ESP32 classic-BT project
 * does regardless of host vs device role. One thing that WASN'T low-risk and
 * had to be fixed after a real hardware test: the enable() mode argument
 * must be ESP_BT_MODE_CLASSIC_BT, not BlueRetro's ESP_BT_MODE_BTDM, since our
 * sdkconfig.defaults configures the controller as BR/EDR-only (see below).
 *
 * REAL BUG FOUND AND FIXED, the second one on real hardware in this file:
 * the original synchronous "busy-wait for ready, then send" design (the file
 * header used to explicitly flag this as the risk to "revisit if you hit
 * 'controller not ready' drops during bring-up") deadlocked for real.
 * bt_device_role.c's HCI Command Complete handler calls straight back into
 * bt_vhci_transport_send() to fire the *next* command, from inside the same
 * call chain as the rx callback that delivered the *previous* command's
 * completion. On real hardware, the controller's "ready for next send"
 * notification (vhci_tx_ready_cb below) doesn't arrive until that whole rx
 * chain unwinds -- so a blocking wait for it, called from inside that same
 * chain, can never succeed. Fixed by decoupling: bt_vhci_transport_send()
 * now just enqueues and returns immediately (safe from any context,
 * including from inside the rx callback chain), and a separate
 * bt_vhci_transport_pump_tx(), called from main.c's ordinary task-context
 * loop, does the actual esp_vhci_host_send_packet() call once the
 * controller reports ready -- this is much closer to what BlueRetro's real
 * host.c actually does (a proper TX queue + separate consumer), which the
 * original version of this file simplified away as supposedly low-risk.
 */
#include <string.h>
#include <esp_bt.h>
#include <esp_err.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "bt_vhci_transport.h"

/* Largest single packet any caller enqueues. bt_device_role.c's HCI commands
 * top out at 4 (H4 type + 2-byte opcode + 1-byte length) + 255 = 259 bytes.
 * bt_l2cap_device_role.c's channel-data sends top out at H4(1) + acl_hdr(4)
 * + l2cap_hdr(4) + up to 303 bytes of payload = 312. Rounded up with a
 * little headroom. */
#define VHCI_TX_MAX_PKT_LEN 320
#define VHCI_TX_QUEUE_LEN 16

typedef struct {
    uint16_t len;
    uint8_t data[VHCI_TX_MAX_PKT_LEN];
} vhci_tx_item_t;

/* atomic-ish flag; the tx_ready callback (producer) can fire from the
 * controller's own context, bt_vhci_transport_pump_tx() (consumer) runs
 * from main.c's task -- a plain volatile is adequate for a single
 * producer/single consumer boolean like this. */
static volatile int g_ctrl_ready = 0;

static bt_vhci_rx_cb_t g_rx_cb = NULL;
static QueueHandle_t g_tx_queue = NULL;

static void vhci_tx_ready_cb(void) {
    g_ctrl_ready = 1;
}

static int vhci_rx_cb(uint8_t *data, uint16_t len) {
    if (g_rx_cb) g_rx_cb(data, len);
    return 0;
}

static esp_vhci_host_callback_t s_vhci_host_cb = {
    vhci_tx_ready_cb,
    vhci_rx_cb,
};

esp_err_t bt_vhci_transport_init(bt_vhci_rx_cb_t rx_cb) {
    esp_err_t ret;

    g_rx_cb = rx_cb;

    g_tx_queue = xQueueCreate(VHCI_TX_QUEUE_LEN, sizeof(vhci_tx_item_t));
    if (g_tx_queue == NULL) {
        printf("# bt_vhci_transport: failed to create TX queue\n");
        return ESP_ERR_NO_MEM;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        printf("# bt_vhci_transport: controller init failed: %s\n", esp_err_to_name(ret));
        return ret;
    }

    /* ESP_BT_MODE_CLASSIC_BT, not ESP_BT_MODE_BTDM (dual BLE+Classic) --
     * board-b-wii/sdkconfig.defaults sets CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y
     * (we only need Classic to emulate a Wiimote), and Espressif's own docs
     * are explicit that the mode passed here "must be equal to the mode in
     * cfg of esp_bt_controller_init()" -- passing BTDM against a
     * BR/EDR-only-configured controller is exactly what produced a real
     * ESP_ERR_INVALID_ARG on actual hardware. BlueRetro's host.c (the
     * pattern this file was originally copied from) needs BTDM because it
     * supports BLE controllers too; we don't. */
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        printf("# bt_vhci_transport: controller enable failed: %s\n", esp_err_to_name(ret));
        return ret;
    }

    esp_vhci_host_register_callback(&s_vhci_host_cb);

    /* Matches BlueRetro's host.c calling this once manually right after
     * registration, rather than waiting for the controller's first async
     * "ready" notification — TODO_VERIFY this is still needed/correct on
     * whatever IDF version you're building against; it was necessary on
     * BlueRetro's targeted version. */
    vhci_tx_ready_cb();

    return ESP_OK;
}

/* NAMED bt_vhci_transport_send, not vhci_send -- see bt_vhci_transport.h for
 * why: ESP-IDF's own precompiled Bluetooth controller blob already defines a
 * global symbol called exactly "vhci_send", which the linker won't allow a
 * second definition of. Caught by an actual link failure, not guessed.
 *
 * Enqueues only -- see bt_vhci_transport.h and the file header above for why
 * this doesn't block/send directly anymore. */
void bt_vhci_transport_send(const uint8_t *hci_cmd, uint16_t len) {
    if (len > VHCI_TX_MAX_PKT_LEN) {
        printf("# bt_vhci_transport: packet len %d exceeds max %d, dropping\n", len, VHCI_TX_MAX_PKT_LEN);
        return;
    }

    vhci_tx_item_t item;
    item.len = len;
    memcpy(item.data, hci_cmd, len);

    /* 0 timeout: never block the caller, which may be the rx callback
     * chain itself. If the queue is genuinely full (16 packets backed up),
     * something is badly wrong upstream; drop and log rather than risk
     * blocking a context we must not block. */
    if (xQueueSend(g_tx_queue, &item, 0) != pdTRUE) {
        printf("# bt_vhci_transport: TX queue full, dropping a %d-byte packet\n", len);
    }
}

/* Call periodically from main.c's ordinary task loop. Sends at most one
 * packet per call -- if more than one is pending and the controller is
 * ready, the next one goes out on the next call instead of trying to drain
 * the whole queue in one go, since esp_vhci_host_send_packet() itself needs
 * a fresh "ready" notification per packet anyway. */
void bt_vhci_transport_pump_tx(void) {
    if (!g_ctrl_ready) return;

    vhci_tx_item_t item;
    if (xQueueReceive(g_tx_queue, &item, 0) != pdTRUE) return;

    g_ctrl_ready = 0;
    esp_vhci_host_send_packet(item.data, item.len);
}
