/*
 * Wiimote input/output report definitions.
 *
 * Written from the publicly documented protocol at
 * https://wiibrew.org/wiki/Wiimote (report ID table, core buttons layout)
 * and cross-referenced against BlueRetro's hidp/wii.c (host-side parsing of
 * the same reports, vendored in ../bt_reference/wii.h) for sanity-checking
 * field order.
 *
 * STATUS: report ID numbers and general structure are solid (multiple
 * independent public sources agree). The exact core-buttons bit-per-button
 * mapping below follows the commonly documented layout but has NOT been
 * checked bit-for-bit against a live capture in this project — do that
 * during Phase 1 of bring-up (see tools/CAPTURE_NOTES.md) before trusting it.
 */
#ifndef WM_REPORTS_H
#define WM_REPORTS_H

#include <stdint.h>

/* Input report IDs (Wiimote -> Host) */
typedef enum {
    WM_IN_STATUS               = 0x20,
    WM_IN_READ_MEM_DATA        = 0x21,
    WM_IN_ACK                  = 0x22,
    WM_IN_BTN                  = 0x30, /* Core buttons only */
    WM_IN_BTN_ACC              = 0x31, /* Core buttons + accelerometer */
    WM_IN_BTN_EXT8             = 0x32, /* Core buttons + 8 extension bytes */
    WM_IN_BTN_ACC_IR12         = 0x33, /* Core buttons + accel + 12 IR bytes */
    WM_IN_BTN_EXT19            = 0x34, /* Core buttons + 19 extension bytes */
    WM_IN_BTN_ACC_EXT16        = 0x35, /* Core buttons + accel + 16 ext bytes */
    WM_IN_BTN_IR10_EXT9        = 0x36,
    WM_IN_BTN_ACC_IR10_EXT6    = 0x37,
} wm_input_report_id_t;

/* Output report IDs (Host -> Wiimote) */
typedef enum {
    WM_OUT_RUMBLE        = 0x10,
    WM_OUT_LED           = 0x11,
    WM_OUT_REPORT_MODE   = 0x12, /* selects which WM_IN_* the host wants streamed */
    WM_OUT_IR_ENABLE     = 0x13,
    WM_OUT_SPEAKER_EN    = 0x14,
    WM_OUT_STATUS_REQ    = 0x15,
    WM_OUT_WRITE_MEM     = 0x16,
    WM_OUT_READ_MEM      = 0x17,
    WM_OUT_SPEAKER_DATA  = 0x18,
    WM_OUT_SPEAKER_MUTE  = 0x19,
    WM_OUT_IR_ENABLE2    = 0x1a,
} wm_output_report_id_t;

/* Core buttons: 2 bytes, sent as the start of every WM_IN_BTN* report.
 * Bit layout per commonly documented WiiBrew Core Buttons table — VERIFY
 * against your own capture, see header comment above. */
#pragma pack(push, 1)
typedef struct {
    uint8_t dpad_left  : 1;
    uint8_t dpad_right : 1;
    uint8_t dpad_down  : 1;
    uint8_t dpad_up    : 1;
    uint8_t plus       : 1;
    uint8_t _unused0   : 3;

    uint8_t two        : 1;
    uint8_t one        : 1;
    uint8_t b          : 1;
    uint8_t a          : 1;
    uint8_t minus      : 1;
    uint8_t _unused1   : 1;
    uint8_t home       : 1;
    uint8_t _unused2   : 1;
} wm_core_buttons_t;

/* 0x31: Core buttons + 3-axis accelerometer (10-bit values, top 8 bits here;
 * 2 LSBs of X/Y/Z are packed into the button bytes' unused fields on a real
 * Wiimote — omitted here for a first pass; add once basic buttons work). */
typedef struct {
    wm_core_buttons_t buttons;
    uint8_t accel_x;
    uint8_t accel_y;
    uint8_t accel_z;
} wm_report_btn_acc_t;
#pragma pack(pop)

/* Fills a core-buttons struct from our own generic wire format
 * (see board-a-controller/include/shared_protocol.h). Implemented in
 * wm_reports.c */
void wm_reports_from_relay_buttons(uint16_t relay_buttons, wm_core_buttons_t *out);

#endif
