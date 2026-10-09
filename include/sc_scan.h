/* SPDX-License-Identifier: 0BSD */
#ifndef SC_SCAN_H
#define SC_SCAN_H
#include <stdint.h>
/* Owned, optional discovery hint. Capture validates full BSSID, channel and
 * protected-frame compatibility before passing this to a byte decoder. */
typedef struct {
    uint8_t bssid[6], ssid[32], ssid_len, channel, protected_frame;
} sc_scan_ap;
#endif
