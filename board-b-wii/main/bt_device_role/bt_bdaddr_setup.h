#ifndef BT_BDADDR_SETUP_H
#define BT_BDADDR_SETUP_H

#include <esp_err.h>

/* Call before bt_vhci_transport_init() / esp_bt_controller_init(). */
esp_err_t bt_bdaddr_force_nintendo_oui(void);

/* Call after the controller is up, to confirm (via serial log) that the
 * resulting Bluetooth address actually carries the Nintendo OUI. */
void bt_bdaddr_log_actual(void);

#endif
