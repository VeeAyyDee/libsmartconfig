#include "sc_touch2_psa.h"
#include "psa/crypto.h"
#include <string.h>

int sc_touch2_psa_decrypt(void *user, const uint8_t key[16],
    const uint8_t iv[16], const uint8_t *cipher, size_t length, uint8_t *plain)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t output[160];
    size_t written = 0, final = 0;
    psa_status_t status, aborted, destroyed;
    int imported = 0, success = 0;
    (void)user;
    if (key == NULL || iv == NULL || cipher == NULL || plain == NULL ||
        length == 0 || length > 144 || length % 16U != 0U) return 0;
    status = psa_crypto_init();
    if (status != PSA_SUCCESS) return 0;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_CBC_NO_PADDING);
    status = psa_import_key(&attributes, key, 16, &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) goto cleanup;
    imported = 1;
    status = psa_cipher_decrypt_setup(&operation, key_id, PSA_ALG_CBC_NO_PADDING);
    if (status != PSA_SUCCESS) goto cleanup;
    status = psa_cipher_set_iv(&operation, iv, 16);
    if (status != PSA_SUCCESS) goto cleanup;
    status = psa_cipher_update(&operation, cipher, length, output, sizeof(output), &written);
    if (status != PSA_SUCCESS || written > sizeof(output)) goto cleanup;
    status = psa_cipher_finish(&operation, output + written, sizeof(output) - written, &final);
    if (status != PSA_SUCCESS || final > sizeof(output) - written || written + final != length)
        goto cleanup;
    success = 1;
cleanup:
    aborted = psa_cipher_abort(&operation);
    destroyed = imported ? psa_destroy_key(key_id) : PSA_SUCCESS;
    if (aborted != PSA_SUCCESS || destroyed != PSA_SUCCESS) success = 0;
    if (success) memcpy(plain, output, length);
    memset(output, 0, sizeof(output));
    return success;
}
