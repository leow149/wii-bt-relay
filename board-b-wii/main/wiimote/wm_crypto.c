/* See wm_crypto.h for the scope note on what this does and doesn't cover. */
#include "wm_crypto.h"

const wm_ext_init_write_t wm_ext_disable_encryption_seq[] = {
    { {0xA4, 0x00, 0xF0}, 0x55 },
    { {0xA4, 0x00, 0xFB}, 0x00 },
};

const int wm_ext_disable_encryption_seq_len =
    sizeof(wm_ext_disable_encryption_seq) / sizeof(wm_ext_disable_encryption_seq[0]);
