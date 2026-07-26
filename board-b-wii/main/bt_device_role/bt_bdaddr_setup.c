/*
 * Forces the ESP32's Bluetooth address to match a SPECIFIC real Wiimote's
 * actual address, rather than just any Nintendo-OUI-carrying value.
 *
 * REAL FINDING, from the community around rnconrad/WiimoteEmulator itself
 * (the reference project this whole device-role approach is modeled on):
 * matching name, Class of Device, and even SDP records is NOT sufficient
 * to get the Wii to accept a completely FRESH sync from an emulated
 * Wiimote -- there's some additional "connectionless checking" the Wii
 * does during first-time discovery that a from-scratch identity can't
 * satisfy. Critically, that same discussion notes the check is skipped
 * once the Wii already trusts a given Bluetooth address from a real,
 * previously-paired Wiimote. So instead of a fresh identity, this claims
 * the EXACT address of a real Wiimote already paired to this specific Wii
 * (captured via a real btmon packet capture during earlier testing:
 * 2C:10:C1:5A:C7:34), so the Wii treats Board B as reconnecting to a
 * remote it already trusts.
 *
 * IMPORTANT: keep the real Wiimote this address belongs to powered OFF
 * while testing Board B this way -- two devices claiming the same
 * Bluetooth address at once will just confuse the Wii, not help it.
 *
 * ESP-IDF derives per-interface MAC addresses from a single base MAC, with
 * a fixed offset per interface: the Bluetooth address is the base MAC with
 * its LAST OCTET incremented by 2 (confirmed via Espressif's own docs, not
 * assumed). So to make the resulting BT address exactly 2C:10:C1:5A:C7:34,
 * the base MAC's last octet must be 0x34 - 2 = 0x32.
 *
 * Must be called BEFORE esp_bt_controller_init() -- esp_base_mac_addr_set()
 * has no effect on interfaces already brought up.
 */
#include <string.h>
#include <stdio.h>
#include <esp_err.h>
#include <esp_mac.h>

#include "bt_bdaddr_setup.h"

/* The real Wiimote's actual Bluetooth address, captured via btmon during a
 * real pairing session to a PC -- not invented, not a generic Nintendo OUI
 * placeholder. See the file header for why this specific address, rather
 * than just any Nintendo-OUI-carrying one, is now being targeted. */
static const uint8_t TARGET_BT_ADDR[6] = { 0x2C, 0x10, 0xC1, 0x5A, 0xC7, 0x34 };

esp_err_t bt_bdaddr_force_nintendo_oui(void) {
    uint8_t base_mac[6];

    base_mac[0] = TARGET_BT_ADDR[0];
    base_mac[1] = TARGET_BT_ADDR[1];
    base_mac[2] = TARGET_BT_ADDR[2];
    base_mac[3] = TARGET_BT_ADDR[3];
    base_mac[4] = TARGET_BT_ADDR[4];
    /* Bluetooth MAC = base MAC with last octet + 2 (per Espressif's
     * documented derivation), so subtract 2 here. TODO_VERIFY: doesn't
     * handle underflow if the target's last byte were below 0x02 -- not an
     * issue for this specific target value (0x34), so not handled generally. */
    base_mac[5] = TARGET_BT_ADDR[5] - 2;

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
        if (memcmp(actual, TARGET_BT_ADDR, 6) != 0) {
            printf("# bt_bdaddr_setup: WARNING - resulting address does not match the targeted real Wiimote address\n");
        }
    }
}
