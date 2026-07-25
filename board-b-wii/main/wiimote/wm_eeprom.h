/*
 * Placeholder Wiimote EEPROM/register space.
 *
 * A real Wiimote exposes a 16KB EEPROM readable/writable via WM_OUT_READ_MEM
 * / WM_OUT_WRITE_MEM (see wm_reports.h), containing accelerometer calibration
 * data and Mii storage among other things. Some games/system menus read
 * calibration bytes at startup.
 *
 * STATUS: this is zeroed placeholder data with documented region offsets as
 * comments, NOT real calibration values. If games behave oddly around motion
 * sensing or refuse to recognize the "remote" as calibrated, this is the
 * first place to look — you'll want to fill in real values captured from an
 * actual Wiimote's EEPROM read (readable over Bluetooth from a real unit
 * using WM_OUT_READ_MEM against address range 0x0000-0x0010 for factory
 * accelerometer calibration, per WiiBrew).
 */
#ifndef WM_EEPROM_H
#define WM_EEPROM_H

#include <stdint.h>

#define WM_EEPROM_SIZE 0x1700 /* WiiBrew documents general-purpose region up to ~0x1770 */

extern uint8_t wm_eeprom[WM_EEPROM_SIZE];

void wm_eeprom_init_placeholder(void);

#endif
