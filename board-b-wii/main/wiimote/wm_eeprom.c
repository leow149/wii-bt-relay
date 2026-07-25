#include <string.h>
#include "wm_eeprom.h"

uint8_t wm_eeprom[WM_EEPROM_SIZE];

void wm_eeprom_init_placeholder(void) {
    memset(wm_eeprom, 0, sizeof(wm_eeprom));

    /* Factory accelerometer calibration lives around 0x0000-0x0010 per
     * WiiBrew's EEPROM memory map. Real Wiimotes ship with real per-unit
     * values here; zero-filling will likely make accelerometer-dependent
     * behavior nonsensical rather than merely "flat." Replace with a real
     * dump if/when motion matters for what you're doing.
     *
     * TODO: capture real bytes via WM_OUT_READ_MEM against a real Wiimote
     * and paste them here as the ground-truth reference. */
}
