#ifndef SC_TOUCH2_H
#define SC_TOUCH2_H
#include <stddef.h>
#include "sc_scan.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sc_touch2 sc_touch2;
typedef int (*sc_touch2_decrypt_fn)(void *user, const uint8_t key[16],
    const uint8_t iv[16], const uint8_t *cipher, size_t length, uint8_t *plain);
typedef struct {
    uint8_t key[16];
    sc_touch2_decrypt_fn decrypt;
    void *user;
} sc_touch2_config;
typedef struct {
    uint8_t ssid[32], password[64], reserved[64];
    uint8_t ssid_len, password_len, reserved_len, port_mark, security_version,
            bssid_crc, ipv4;
} sc_touch2_result;
sc_touch2 *sc_touch2_create(const sc_touch2_config *config);
/* Optional copied scan hint; NULL clears it. Metadata remains mandatory. */
void sc_touch2_set_ap(sc_touch2 *ctx, const sc_scan_ap *ap);
void sc_touch2_destroy(sc_touch2 *ctx);
void sc_touch2_reset(sc_touch2 *ctx);
int sc_touch2_feed(sc_touch2 *ctx, uint16_t normalized_length);
int sc_touch2_get_result(const sc_touch2 *ctx, sc_touch2_result *out);
int sc_touch2_make_ack(const sc_touch2_result *result, const uint8_t mac[6],
                        uint8_t out[7], uint16_t *port);
#ifdef __cplusplus
}
#endif
#endif
