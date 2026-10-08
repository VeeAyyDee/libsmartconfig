#ifndef SC_TOUCH2_PSA_H
#define SC_TOUCH2_PSA_H
#include "sc_touch2.h"
#ifdef __cplusplus
extern "C" {
#endif
/* AES-128 CBC without padding removal. Returns 1 only for exact output. */
int sc_touch2_psa_decrypt(void *user, const uint8_t key[16],
    const uint8_t iv[16], const uint8_t *cipher, size_t length, uint8_t *plain);
#ifdef __cplusplus
}
#endif
#endif
