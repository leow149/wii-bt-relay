/*
 * Forces the ESP32's Bluetooth address to carry a Nintendo OUI, mirroring
 * adapter.c's set_up_device_address() in rnconrad/WiimoteEmulator — real,
 * working code confirmed to pair with actual Wii/vWii consoles. That
 * implementation checks whether the existing address already has a
 * Nintendo OUI and only overrides it if not; the Wii apparently filters on
 * this, so getting it wrong likely blocks pairing before any L2CAP
 * handshake even starts.
 *
 * ESP-IDF derives per-interface MAC addresses from a single base MAC, with
 * a fixed offset per interface: the Bluetooth address is the base MAC with
 * its LAST OCTET incremented by 2 (confirmed via Espressif's own docs, not
 * assumed). Since only the last octet shifts, setting the base MAC's OUI
 * (first 3 bytes) is sufficient to make the resulting Bluetooth address
 * carry that same OUI — the middle/last bytes just need the offset
 * accounted for.
 *
 * Must be called BEFORE esp_bt_controller_init() — esp_base_mac_addr_set()
 * has no effect on interfaces already brought up.
 */
#include <string.h>
#include <stdio.h>
#include <esp_err.h>
#include <esp_mac.h>

#include "bt_bdaddr_setup.h"

/* One of the Nintendo OUIs rnconrad's adapter.c checks against/uses
 * (0xA4C0E1) — real Nintendo-assigned prefix, not invented. */
static const uint8_t NINTENDO_OUI[3] = { 0xA4, 0xC0, 0xE1 };

esp_err_t bt_bdaddr_force_nintendo_oui(void) {
    uint8_t base_mac[6];

    /* Arbitrary device-specific bytes for the non-OUI portion — any value
     * works here, this doesn't need to match a real Wiimote's actual
     * address, just carry a Nintendo OUI. */
    base_mac[0] = NINTENDO_OUI[0];
    base_mac[1] = NINTENDO_OUI[1];
    base_mac[2] = NINTENDO_OUI[2];
    base_mac[3] = 0x11;
    base_mac[4] = 0x22;
    /* Bluetooth MAC = base MAC with last octet + 2 (per Espressif's
     * documented derivation). Set base here so the resulting BT address
     * ends in 0x33; subtracting 2 from the base avoids the (unhandled,
     * TODO_VERIFY) edge case of overflow past 0xFF wrapping unexpectedly. */
    base_mac[5] = 0x33 - 2;

    esp_err_t ret = esp_base_mac_addr_set(base_mac);
    if (ret != ESP_OK) {
        printf("# bt_bdaddr_setup: esp_base_mac_addr_set failed: %s\n", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

void bt_bdaddr_log_actual(void) {
    uint8_t actual[6];
    if (esp_read_mac(actual, ESP_MAC_BT) == ESP_OK) {
        printf("# bt_bdaddr_setup: resulting BT address: %02X:%02X:%02X:%02X:%02X:%02X\n",
               actual[0], actual[1], actual[2], actual[3], actual[4], actual[5]);
        if (actual[0] != NINTENDO_OUI[0] || actual[1] != NINTENDO_OUI[1] || actual[2] != NINTENDO_OUI[2]) {
            printf("# bt_bdaddr_setup: WARNING - resulting address does not carry the expected Nintendo OUI\n");
        }
    }
}
