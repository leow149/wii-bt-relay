#ifndef BT_BDADDR_SETUP_H
#define BT_BDADDR_SETUP_H

#include <esp_err.h>

/* Call before bt_vhci_transport_init() / esp_bt_controller_init(). Despite
 * the name, this now targets a SPECIFIC real Wiimote's captured address
 * (still carries a Nintendo OUI, just no longer an arbitrary one) -- see
 * bt_bdaddr_setup.c for why. */
esp_err_t bt_bdaddr_force_nintendo_oui(void);

/* Call after the controller is up, to confirm (via serial log) that the
 * resulting Bluetooth address actually matches the targeted real Wiimote
 * address. */
void bt_bdaddr_log_actual(void);

#endif
