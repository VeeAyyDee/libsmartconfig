#ifndef TEST_PSA_CRYPTO_H
#define TEST_PSA_CRYPTO_H
/* Behavioral API doubles; numeric constants and layouts are not SDK ABI. */
#include <stddef.h>
#include <stdint.h>
typedef int psa_status_t;
typedef unsigned int mbedtls_svc_key_id_t;
typedef unsigned int psa_algorithm_t;
typedef struct { unsigned int type, usage, algorithm; size_t bits; } psa_key_attributes_t;
typedef struct { unsigned int active; } psa_cipher_operation_t;
#define PSA_SUCCESS 0
#define PSA_KEY_ATTRIBUTES_INIT {0, 0, 0, 0}
#define PSA_CIPHER_OPERATION_INIT {0}
#define MBEDTLS_SVC_KEY_ID_INIT 0
#define PSA_KEY_TYPE_AES 1U
#define PSA_KEY_USAGE_DECRYPT 2U
#define PSA_ALG_CBC_NO_PADDING 3U
psa_status_t psa_crypto_init(void);
void psa_set_key_type(psa_key_attributes_t *attributes, unsigned int type);
void psa_set_key_bits(psa_key_attributes_t *attributes, size_t bits);
void psa_set_key_usage_flags(psa_key_attributes_t *attributes, unsigned int usage);
void psa_set_key_algorithm(psa_key_attributes_t *attributes, psa_algorithm_t algorithm);
void psa_reset_key_attributes(psa_key_attributes_t *attributes);
psa_status_t psa_import_key(const psa_key_attributes_t *attributes, const uint8_t *data,
                            size_t length, mbedtls_svc_key_id_t *key);
psa_status_t psa_destroy_key(mbedtls_svc_key_id_t key);
psa_status_t psa_cipher_decrypt_setup(psa_cipher_operation_t *operation, mbedtls_svc_key_id_t key, psa_algorithm_t algorithm);
psa_status_t psa_cipher_set_iv(psa_cipher_operation_t *operation, const uint8_t *iv, size_t length);
psa_status_t psa_cipher_update(psa_cipher_operation_t *operation, const uint8_t *input, size_t input_length,
                               uint8_t *output, size_t output_size, size_t *output_length);
psa_status_t psa_cipher_finish(psa_cipher_operation_t *operation, uint8_t *output, size_t output_size, size_t *output_length);
psa_status_t psa_cipher_abort(psa_cipher_operation_t *operation);
#endif
