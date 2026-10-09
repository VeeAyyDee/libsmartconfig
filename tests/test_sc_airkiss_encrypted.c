/* SPDX-License-Identifier: 0BSD */
#include "sc_airkiss.h"
#include "sc_airkiss_capture.h"
#include "sc_touch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
typedef struct { size_t key_len, password_len, cipher_len; uint8_t key[16], password[64], cipher[80]; } crypto_fixture;
#include "airkiss_crypto_fixtures.h"
typedef struct { const crypto_fixture *f; unsigned int calls; int fault; } crypto_spy;
static int decrypt(void *user, const uint8_t key[16], const uint8_t iv[16], const uint8_t *cipher, size_t length, uint8_t *plain)
{
    crypto_spy *spy = user;
    const crypto_fixture *f = spy->f;
    ++spy->calls;
    CHECK(memcmp(key, f->key, 16) == 0 && memcmp(iv, f->key, 16) == 0);
    CHECK(length == f->cipher_len && memcmp(cipher, f->cipher, length) == 0 && plain != cipher);
    memset(plain, (int)(length - f->password_len), length);
    memcpy(plain, f->password, f->password_len);
    if (spy->fault == 1) return 0;
    if (spy->fault == 2) plain[length - 1] = 0;
    if (spy->fault == 3) plain[f->password_len] ^= 1U; /* Includes non-last suffix corruption. */
    if (spy->fault == 4) plain[length - 1] = 17;
    if (spy->fault == 5) memset(plain, 16, length); /* Padding-only for a 16-byte ciphertext. */
    return 1;
}
static sc_airkiss_config config_for(crypto_spy *spy)
{
    sc_airkiss_config config = {0};
    memcpy(config.key, spy->f->key, 16); config.key_len = spy->f->key_len;
    config.decrypt = decrypt; config.user = spy;
    return config;
}
static size_t message_lengths(const crypto_fixture *f, size_t ssid_len, int empty, uint16_t out[190])
{
    uint8_t bytes[113] = {0}, pair[5], wire_password = (uint8_t)(empty ? 0 : f->cipher_len);
    size_t total = wire_password + 1U + ssid_len, i, index, used = 0;
    unsigned int crc;
    if (wire_password != 0) memcpy(bytes, f->cipher, wire_password);
    bytes[wire_password] = 0x5a;
    for (i = 0; i < ssid_len; ++i) bytes[wire_password + 1U + i] = (uint8_t)(i * 37U);
    crc = sc_touch_crc8(bytes + wire_password + 1U, ssid_len);
    out[used++] = (uint16_t)(total < 16 ? 8 : total >> 4);
    out[used++] = (uint16_t)(16U + (total & 15U));
    out[used++] = (uint16_t)(32U + (crc >> 4)); out[used++] = (uint16_t)(48U + (crc & 15U));
    crc = sc_touch_crc8(&wire_password, 1);
    out[used++] = (uint16_t)(64U + (wire_password >> 4)); out[used++] = (uint16_t)(80U + (wire_password & 15U));
    out[used++] = (uint16_t)(96U + (crc >> 4)); out[used++] = (uint16_t)(112U + (crc & 15U));
    for (index = (total + 3U) / 4U; index > 0; --index) {
        size_t block = index - 1, count = total - 4U * block;
        if (count > 4) count = 4;
        pair[0] = (uint8_t)block; memcpy(pair + 1, bytes + 4U * block, count);
        out[used++] = (uint16_t)(128U + (sc_touch_crc8(pair, count + 1) & 127U));
        out[used++] = (uint16_t)(128U + block);
        for (i = 0; i < count; ++i) out[used++] = (uint16_t)(256U + pair[i + 1]);
    }
    CHECK(used <= 190); return used;
}
static int feed(sc_airkiss *ctx, const uint16_t *lengths, size_t count)
{
    size_t i; int state = 0;
    for (i = 0; i < count; ++i) state = sc_airkiss_feed(ctx, lengths[i]);
    return state;
}
static void unavailable(sc_airkiss *ctx)
{
    sc_airkiss_result result; unsigned char before[sizeof(result)];
    memset(&result, 0xa5, sizeof(result)); memcpy(before, &result, sizeof(result));
    CHECK(sc_airkiss_get_result(ctx, &result) == 0 && memcmp(before, &result, sizeof(result)) == 0);
}
static void test_vectors_and_failures(void)
{
    size_t v, s; int fault;
    for (v = 0; v < sizeof(crypto_vectors) / sizeof(crypto_vectors[0]); ++v)
    for (s = 0; s <= 32; s += 8) for (fault = 0; fault < 5; ++fault) {
        crypto_spy spy = {&crypto_vectors[v], 0, fault};
        sc_airkiss_config config = config_for(&spy);
        sc_airkiss *ctx = sc_airkiss_create_with_config(&config);
        sc_airkiss_result result;
        uint16_t lengths[190]; size_t count = message_lengths(spy.f, s, 0, lengths);
        CHECK(ctx != NULL);
        memset(&config, 0xff, sizeof(config)); /* Caller config/key memory is not retained. */
        CHECK(feed(ctx, lengths, count) == (fault == 0 ? 1 : 0));
        CHECK(spy.calls == 1);
        if (fault == 0) {
            CHECK(sc_airkiss_get_result(ctx, &result) == 1);
            CHECK(result.password_len == spy.f->password_len && result.ssid_len == s && result.token == 0x5a);
            CHECK(memcmp(result.password, spy.f->password, spy.f->password_len) == 0);
        } else {
            unavailable(ctx); spy.fault = 0;
            CHECK(feed(ctx, lengths, count) == 0 && spy.calls == 1); /* Reset required after failure. */
        }
        spy.fault = 0; sc_airkiss_reset(ctx); unavailable(ctx);
        CHECK(feed(ctx, lengths, count) == 1 && spy.calls == 2);
        sc_airkiss_destroy(ctx);
    }
}
static void test_empty_and_config(void)
{
    crypto_fixture zero_key = crypto_vectors[0];
    crypto_spy spy = {&zero_key, 0, 0};
    sc_airkiss_config config;
    sc_airkiss *ctx, *other;
    uint16_t lengths[190]; size_t count;
    memset(zero_key.key, 0, sizeof(zero_key.key)); zero_key.key_len = 16;
    config = config_for(&spy); ctx = sc_airkiss_create_with_config(&config);
    other = sc_airkiss_create_with_config(&config); CHECK(ctx != NULL && other != NULL);
    count = message_lengths(&zero_key, 32, 0, lengths);
    CHECK(feed(ctx, lengths, count) == 1); unavailable(other);
    CHECK(feed(other, lengths, count) == 1); sc_airkiss_destroy(other); sc_airkiss_destroy(ctx);
    ctx = sc_airkiss_create_with_config(&config); CHECK(ctx != NULL);
    count = message_lengths(&zero_key, 32, 1, lengths); spy.calls = 0;
    CHECK(feed(ctx, lengths, count) == 1 && spy.calls == 0);
    sc_airkiss_reset(ctx); spy.fault = 5;
    count = message_lengths(&zero_key, 32, 0, lengths);
    CHECK(feed(ctx, lengths, count) == 0); unavailable(ctx); sc_airkiss_destroy(ctx);
    config.key_len = 0; CHECK(sc_airkiss_create_with_config(&config) == NULL);
    CHECK(sc_airkiss_capture_create_with_config(&config) == NULL);
    config.key_len = 17; CHECK(sc_airkiss_create_with_config(&config) == NULL);
    config.key_len = 1; config.decrypt = NULL; CHECK(sc_airkiss_create_with_config(&config) == NULL);
    ctx = sc_airkiss_create_with_config(NULL); CHECK(ctx != NULL); sc_airkiss_destroy(ctx);
}
static void test_cipher_metadata_bounds(void)
{
    crypto_fixture f = crypto_vectors[0]; crypto_spy spy = {&f, 0, 0};
    sc_airkiss_config config = config_for(&spy);
    sc_airkiss *ctx = sc_airkiss_create_with_config(&config);
    uint16_t lengths[190]; size_t count;
    CHECK(ctx != NULL);
    f.cipher_len = 15; count = message_lengths(&f, 1, 0, lengths);
    CHECK(feed(ctx, lengths, count) == 0 && spy.calls == 0); unavailable(ctx);
    sc_airkiss_reset(ctx); f.cipher_len = 16;
    count = message_lengths(&f, 1, 0, lengths);
    lengths[0] = 8; lengths[1] = 31; /* Total 15 cannot contain ciphertext16 + token. */
    CHECK(feed(ctx, lengths, count) == 0 && spy.calls == 0); unavailable(ctx);
    sc_airkiss_destroy(ctx);
}
static void test_capture_reset(void)
{
    crypto_spy spy = {&crypto_vectors[35], 0, 0}; sc_airkiss_config config = config_for(&spy);
    sc_airkiss_capture *ctx = sc_airkiss_capture_create_with_config(&config);
    uint8_t h[26] = {0}; uint16_t lengths[190];
    size_t count = message_lengths(spy.f, 32, 0, lengths), i; unsigned int pass, number = 0;
    CHECK(ctx != NULL); h[0] = 8; h[1] = 2; memset(h + 4, 255, 6); h[10] = 2; h[16] = 4;
    for (pass = 0; pass < 2; ++pass) {
        sc_airkiss_result result;
        for (i = 0; i < count + 8; ++i) {
            uint16_t sequence = (uint16_t)((number++ & 4095U) << 4);
            uint16_t length = i < 8 ? (uint16_t)(1U + i % 4U) : lengths[i - 8];
            int expected = i < 3 ? 0 : i + 1 == count + 8 ? 2 : 1;
            h[22] = (uint8_t)sequence; h[23] = (uint8_t)(sequence >> 8);
            CHECK(sc_airkiss_capture_feed(ctx, h, 24, (uint16_t)(length + 64U), 6, (uint32_t)i) == expected);
        }
        CHECK(sc_airkiss_capture_get_result(ctx, &result) == 1 && result.password_len == 64);
        CHECK(memcmp(result.password, spy.f->password, 64) == 0);
        sc_airkiss_capture_reset(ctx);
        CHECK(sc_airkiss_capture_get_result(ctx, &result) == 0);
    }
    CHECK(spy.calls == 2); sc_airkiss_capture_destroy(ctx);
}
int main(void)
{
    test_vectors_and_failures(); test_empty_and_config(); test_cipher_metadata_bounds(); test_capture_reset();
    puts("PASS: encrypted AirKiss callback/metadata/padding/config/capture regressions (callbacks are not AES validation)");
    return 0;
}
