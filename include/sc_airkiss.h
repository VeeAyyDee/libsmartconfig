#ifndef SC_AIRKISS_H
#define SC_AIRKISS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sc_airkiss sc_airkiss;
typedef int (*sc_airkiss_decrypt_fn)(void *user, const uint8_t key[16],
    const uint8_t iv[16], const uint8_t *cipher, size_t length, uint8_t *plain);
typedef struct {
    uint8_t key[16];
    size_t key_len;
    sc_airkiss_decrypt_fn decrypt;
    void *user;
} sc_airkiss_config;
typedef struct {
    uint8_t ssid[32], password[64];
    uint8_t ssid_len, password_len, token;
} sc_airkiss_result;
sc_airkiss *sc_airkiss_create(void);
/* NULL config selects plaintext. Non-NULL requires key_len 1..16 and decrypt.
 * The copied key is zero-padded to 16 bytes and used as both AES key and IV.
 * Callback writes exactly length (16..80) decrypted bytes without unpadding. */
sc_airkiss *sc_airkiss_create_with_config(const sc_airkiss_config *config);
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
