#ifndef SC_TOUCH_H
#define SC_TOUCH_H

#include <stddef.h>
#include "sc_scan.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sc_touch sc_touch;

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    uint8_t bssid[6];
    uint8_t sender_ip[4];
    uint8_t ssid_len;
    uint8_t password_len;
} sc_touch_result;

/* NULL with any length returns zero. */
uint8_t sc_touch_crc8(const uint8_t *bytes, size_t length);

/* Returns 1 on success, otherwise 0 and leaves outputs untouched. */
int sc_touch_decode_triplet(const uint16_t lengths[3],
                            uint8_t *index, uint8_t *value);

sc_touch *sc_touch_create(void);
/* Optional copied scan hint; NULL clears it. Metadata remains mandatory. */
void sc_touch_set_ap(sc_touch *ctx, const sc_scan_ap *ap);
void sc_touch_destroy(sc_touch *ctx);
void sc_touch_reset(sc_touch *ctx);

/* 1: completed; 0: incomplete; -1: NULL context; -2: conflict until reset. */
int sc_touch_feed(sc_touch *ctx, uint16_t udp_payload_length);

/* Copies only a completed result. Returns 0 without modifying out otherwise. */
int sc_touch_get_result(const sc_touch *ctx, sc_touch_result *out);

#ifdef __cplusplus
}
#endif

#endif
