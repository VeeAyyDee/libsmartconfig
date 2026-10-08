#include "sc_touch2_psa.h"
#include "psa/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
enum { INIT, IMPORT, SETUP, IV, UPDATE, FINISH, ABORT, DESTROY, OPERATIONS };
static struct {
    uint8_t key[16], iv[16], cipher[144], plain[144];
    size_t length;
    unsigned int calls[OPERATIONS];
    int fail, bad_length;
} mock;
static psa_status_t called(int operation)
{ ++mock.calls[operation]; return mock.fail == operation ? -1 : PSA_SUCCESS; }
psa_status_t psa_crypto_init(void) { return called(INIT); }
void psa_set_key_type(psa_key_attributes_t *a, unsigned int v) { a->type = v; }
void psa_set_key_bits(psa_key_attributes_t *a, size_t v) { a->bits = v; }
void psa_set_key_usage_flags(psa_key_attributes_t *a, unsigned int v) { a->usage = v; }
void psa_set_key_algorithm(psa_key_attributes_t *a, psa_algorithm_t v) { a->algorithm = v; }
void psa_reset_key_attributes(psa_key_attributes_t *a) { memset(a, 0, sizeof(*a)); }
psa_status_t psa_import_key(const psa_key_attributes_t *a, const uint8_t *data, size_t length, mbedtls_svc_key_id_t *key)
{
    CHECK(a->type == PSA_KEY_TYPE_AES && a->bits == 128 && a->usage == PSA_KEY_USAGE_DECRYPT);
    CHECK(a->algorithm == PSA_ALG_CBC_NO_PADDING && length == 16 && memcmp(data, mock.key, 16) == 0);
    if (called(IMPORT) != PSA_SUCCESS) return -1;
    *key = 71; return PSA_SUCCESS;
}
psa_status_t psa_destroy_key(mbedtls_svc_key_id_t key)
{ CHECK(key == 71); return called(DESTROY); }
psa_status_t psa_cipher_decrypt_setup(psa_cipher_operation_t *op, mbedtls_svc_key_id_t key, psa_algorithm_t algorithm)
{
    CHECK(key == 71 && algorithm == PSA_ALG_CBC_NO_PADDING && op->active == 0);
    op->active = 1; return called(SETUP);
}
psa_status_t psa_cipher_set_iv(psa_cipher_operation_t *op, const uint8_t *iv, size_t length)
{ CHECK(op->active && length == 16 && memcmp(iv, mock.iv, 16) == 0); return called(IV); }
psa_status_t psa_cipher_update(psa_cipher_operation_t *op, const uint8_t *input, size_t length,
                               uint8_t *output, size_t capacity, size_t *written)
{
    CHECK(op->active && length == mock.length && capacity >= length);
    CHECK(memcmp(input, mock.cipher, length) == 0);
    if (called(UPDATE) != PSA_SUCCESS) return -1;
    *written = length - 16; memcpy(output, mock.plain, *written);
    if (mock.bad_length == 2) *written = 161;
    return PSA_SUCCESS;
}
psa_status_t psa_cipher_finish(psa_cipher_operation_t *op, uint8_t *output, size_t capacity, size_t *written)
{
    CHECK(op->active && capacity >= 16);
    if (called(FINISH) != PSA_SUCCESS) return -1;
    *written = mock.bad_length == 1 ? 15U : 16U;
    memcpy(output, mock.plain + mock.length - 16, *written); op->active = 0;
    return PSA_SUCCESS;
}
psa_status_t psa_cipher_abort(psa_cipher_operation_t *op)
{ op->active = 0; return called(ABORT); }
static void setup(size_t length, int fail, int bad_length)
{
    size_t i;
    memset(&mock, 0, sizeof(mock)); mock.length = length; mock.fail = fail; mock.bad_length = bad_length;
    for (i = 0; i < 16; ++i) { mock.key[i] = (uint8_t)(i + 1); mock.iv[i] = (uint8_t)(i + 17); }
    for (i = 0; i < length; ++i) { mock.cipher[i] = (uint8_t)(i * 31U); mock.plain[i] = (uint8_t)(i * 53U); }
}
int main(void)
{
    uint8_t plain[144], before[144];
    size_t length;
    int fail, bad;
    for (length = 16; length <= 144; length += 16) for (fail = -1; fail < OPERATIONS; ++fail)
    for (bad = 0; bad < 3; ++bad) {
        int success = fail == -1 && bad == 0;
        setup(length, fail, bad); memset(plain, 0xa5, sizeof(plain)); memcpy(before, plain, sizeof(plain));
        CHECK(sc_touch2_psa_decrypt(NULL, mock.key, mock.iv, mock.cipher, length, plain) == success);
        if (success) CHECK(memcmp(plain, mock.plain, length) == 0);
        else CHECK(memcmp(plain, before, sizeof(plain)) == 0);
        CHECK(mock.calls[ABORT] == (fail == INIT ? 0U : 1U));
        CHECK(mock.calls[DESTROY] == (fail == INIT || fail == IMPORT ? 0U : 1U));
    }
    setup(16, -1, 0);
    CHECK(sc_touch2_psa_decrypt(NULL, NULL, mock.iv, mock.cipher, 16, plain) == 0);
    CHECK(sc_touch2_psa_decrypt(NULL, mock.key, NULL, mock.cipher, 16, plain) == 0);
    CHECK(sc_touch2_psa_decrypt(NULL, mock.key, mock.iv, NULL, 16, plain) == 0);
    CHECK(sc_touch2_psa_decrypt(NULL, mock.key, mock.iv, mock.cipher, 16, NULL) == 0);
    CHECK(sc_touch2_psa_decrypt(NULL, mock.key, mock.iv, mock.cipher, 0, plain) == 0);
    CHECK(sc_touch2_psa_decrypt(NULL, mock.key, mock.iv, mock.cipher, 17, plain) == 0);
    CHECK(sc_touch2_psa_decrypt(NULL, mock.key, mock.iv, mock.cipher, 160, plain) == 0);
    CHECK(mock.calls[INIT] == 0);
    puts("PASS: PSA wrapper API/failure/cleanup/output-bound mocks; this is not AES cryptographic validation");
    return 0;
}
