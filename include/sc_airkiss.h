#ifndef SC_AIRKISS_H
#define SC_AIRKISS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sc_airkiss sc_airkiss;
typedef struct {
    uint8_t ssid[32], password[64];
    uint8_t ssid_len, password_len, token;
} sc_airkiss_result;
sc_airkiss *sc_airkiss_create(void);
void sc_airkiss_destroy(sc_airkiss *ctx);
void sc_airkiss_reset(sc_airkiss *ctx);
/* 0 incomplete, 1 complete, -1 NULL, -2 conflicting session until reset. */
int sc_airkiss_feed(sc_airkiss *ctx, uint16_t normalized_length);
int sc_airkiss_get_result(const sc_airkiss *ctx, sc_airkiss_result *out);
int sc_airkiss_make_ack(const sc_airkiss_result *result, uint8_t out[1]);
#ifdef __cplusplus
}
#endif
#endif
