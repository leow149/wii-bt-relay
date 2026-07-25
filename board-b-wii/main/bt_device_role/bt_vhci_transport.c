/*
 * VHCI transport wiring for the device-role stack.
 *
 * Unlike the rest of bt_device_role/, this file's init sequence and callback
 * registration pattern is copied directly from the real, working pattern in
 * ../bt_reference/host.c (BlueRetro's host-role bring-up) — esp_bt_controller_init,
 * esp_bt_controller_enable(ESP_BT_MODE_BTDM), esp_vhci_host_register_callback
 * with a {tx_ready, rx_pkt} callback struct are all real ESP-IDF esp_bt.h API,
 * confirmed against working code rather than recalled from memory. This part
 * is genuinely low-risk — it's the same bring-up any ESP32 classic-BT project
 * does regardless of host vs device role.
 *
 * Simplified vs. BlueRetro's version: no ring buffer / separate TX task, since
 * this stack sends far less traffic (one peripheral link, not N host
 * connections). A synchronous send-when-ready is adequate here; revisit if
 * you hit "controller not ready" drops during bring-up.
 */
#include <string.h>
#include <esp_bt.h>
#include <esp_err.h>
#include <stdio.h>

#include "bt_vhci_transport.h"

/* atomic-ish flag; single producer (VHCI callback) / single consumer
 * (bt_vhci_transport_send busy-wait) so a plain volatile is adequate here.
 * BlueRetro uses a proper atomic bit + ring buffer since it juggles multiple
 * simultaneous links; revisit if this stack grows the same requirement. */
static volatile int g_ctrl_ready = 0;

static bt_vhci_rx_cb_t g_rx_cb = NULL;

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

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        printf("# bt_vhci_transport: controller init failed: %s\n", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
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
 * second definition of. Caught by an actual link failure, not guessed. */
void bt_vhci_transport_send(const uint8_t *hci_cmd, uint16_t len) {
    /* Busy-wait for controller ready. Fine for a single low-traffic
     * peripheral link; see file header note if this needs to become
     * queue-based later. */
    int spins = 0;
    while (!g_ctrl_ready) {
        spins++;
        if (spins > 1000000) {
            printf("# bt_vhci_transport: bt_vhci_transport_send timed out waiting for ready\n");
            return;
        }
    }
    g_ctrl_ready = 0;
    esp_vhci_host_send_packet((uint8_t *)hci_cmd, len);
}
