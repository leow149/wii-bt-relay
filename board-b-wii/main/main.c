/*
 * Board B entry point. See docs/STATUS.md for what's proven vs experimental.
 *
 * bt_bdaddr_force_nintendo_oui() must run BEFORE bt_vhci_transport_init(),
 * since esp_base_mac_addr_set() has no effect on interfaces already
 * brought up -- see bt_bdaddr_setup.c for why this matters (the Wii
 * appears to filter on Bluetooth OUI, per rnconrad/WiimoteEmulator's
 * adapter.c, confirmed working against real Wii/vWii hardware).
 */
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "bt_bdaddr_setup.h"
#include "bt_device_role.h"
#include "bt_vhci_transport.h"
#include "wm_eeprom.h"
#include "uart_bridge.h"

void app_main(void) {
    printf("wii-bt-relay Board B starting (experimental, see docs/STATUS.md)\n");

    wm_eeprom_init_placeholder();
    uart_bridge_init();

    esp_err_t err = bt_bdaddr_force_nintendo_oui();
    if (err != ESP_OK) {
        printf("# bt_bdaddr_force_nintendo_oui failed, continuing anyway "
               "(pairing will likely fail without a Nintendo OUI)\n");
    }

    err = bt_vhci_transport_init(bt_device_role_on_hci_event);
    if (err != ESP_OK) {
        printf("# bt_vhci_transport_init failed, halting\n");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    bt_bdaddr_log_actual();

    bt_device_role_init();

    while (1) {
        /* bt_vhci_transport_pump_tx() MUST run from here, not from inside
         * bt_device_role_on_hci_event()'s call chain -- see
         * bt_vhci_transport.c for the real deadlock a hardware test found
         * when sends were attempted directly from the rx callback context. */
        bt_vhci_transport_pump_tx();
        bt_device_role_poll();
        uart_bridge_poll();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
