#include "sc_airkiss.h"
#include "sc_touch.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_TOTAL = 113, MAX_BLOCKS = 29, MAX_CIPHER = 80 };
struct sc_airkiss {
    sc_airkiss_config config;
    sc_scan_ap ap;
    uint8_t bytes[MAX_TOTAL], seen[MAX_BLOCKS];
    uint16_t metadata[4];
    size_t metadata_count, payload_count;
    unsigned int header_count;
    uint8_t block_crc, block_index, payload[4];
    uint8_t total, ssid_crc, password_length;
    int have_magic, have_prefix, poisoned, complete, crypto_failed;
    sc_airkiss_result result;
};

static void clear_bytes(void *memory, size_t length)
{
    volatile uint8_t *bytes = memory;
    while (length != 0) { *bytes++ = 0; --length; }
}
sc_airkiss *sc_airkiss_create_with_config(const sc_airkiss_config *config)
{
    sc_airkiss *ctx;
    if (config != NULL && (config->key_len == 0 || config->key_len > 16 || config->decrypt == NULL))
        return NULL;
    ctx = calloc(1, sizeof(*ctx));
    if (ctx != NULL && config != NULL) {
        ctx->config.key_len = config->key_len;
        memcpy(ctx->config.key, config->key, config->key_len);
        ctx->config.decrypt = config->decrypt;
        ctx->config.user = config->user;
    }
    return ctx;
}
sc_airkiss *sc_airkiss_create(void) { return sc_airkiss_create_with_config(NULL); }
void sc_airkiss_set_ap(sc_airkiss *ctx, const sc_scan_ap *ap)
{ if (ctx != NULL && !ctx->complete) { memset(&ctx->ap, 0, sizeof(ctx->ap)); if (ap != NULL && ap->ssid_len <= 32) ctx->ap = *ap; } }
static int recover_ssid(const sc_airkiss *ctx)
{
    return ctx->config.key_len == 0 && ctx->have_magic && ctx->have_prefix && ctx->ap.ssid_len != 0 &&
        ctx->ap.ssid_len == ctx->total - ctx->password_length - 1U &&
        sc_touch_crc8(ctx->ap.ssid, ctx->ap.ssid_len) == ctx->ssid_crc;
}

void sc_airkiss_reset(sc_airkiss *ctx)
{
    sc_airkiss_config config;
    if (ctx == NULL) return;
    config = ctx->config;
    clear_bytes(ctx, sizeof(*ctx));
    ctx->config = config;
    clear_bytes(&config, sizeof(config));
}
void sc_airkiss_destroy(sc_airkiss *ctx)
{ if (ctx != NULL) { clear_bytes(ctx, sizeof(*ctx)); free(ctx); } }

static int quartet_is(const uint16_t symbols[4], unsigned int initial_tag)
{
    size_t i;
    for (i = 0; i < 4; ++i)
        if ((symbols[i] >> 4) != initial_tag + i) return 0;
    return 1;
}

static void read_metadata(sc_airkiss *ctx, uint16_t length)
{
    unsigned int total, high, password, checksum;
    uint8_t password_byte;
    if (ctx->metadata_count == 4) {
        memmove(ctx->metadata, ctx->metadata + 1, 3 * sizeof(ctx->metadata[0]));
        ctx->metadata_count = 3;
    }
    ctx->metadata[ctx->metadata_count++] = length;
    if (ctx->metadata_count != 4) return;
    if (quartet_is(ctx->metadata, 0)) {
        high = ctx->metadata[0];
        if (high == 8) high = 0;
        else if (high < 1 || high > (ctx->config.key_len != 0 ? 7U : 6U)) return;
        total = (high << 4) | (ctx->metadata[1] & 15U);
        checksum = ((ctx->metadata[2] & 15U) << 4) | (ctx->metadata[3] & 15U);
        if (total < 1 || total > (ctx->config.key_len != 0 ? MAX_TOTAL : 97U)) return;
        if (ctx->have_magic) {
            if (ctx->total != total || ctx->ssid_crc != checksum) ctx->poisoned = 1;
        } else {
            ctx->total = (uint8_t)total;
            ctx->ssid_crc = (uint8_t)checksum;
            ctx->have_magic = 1;
        }
    } else if (ctx->have_magic && quartet_is(ctx->metadata, 4)) {
        password = ((ctx->metadata[0] & 15U) << 4) | (ctx->metadata[1] & 15U);
        checksum = ((ctx->metadata[2] & 15U) << 4) | (ctx->metadata[3] & 15U);
        password_byte = (uint8_t)password;
        if (password > (ctx->config.key_len != 0 ? MAX_CIPHER : 64U) ||
            (ctx->config.key_len != 0 && password % 16U != 0U) || password >= ctx->total ||
            ctx->total - password - 1U > 32U ||
            sc_touch_crc8(&password_byte, 1) != checksum) return;
        if (ctx->have_prefix && ctx->password_length != password) ctx->poisoned = 1;
        else { ctx->password_length = password_byte; ctx->have_prefix = 1; }
    }
}

static void try_complete(sc_airkiss *ctx)
{
    int recovered = recover_ssid(ctx);
    size_t i, required = recovered ? (size_t)ctx->password_length + 1U : ctx->total;
    size_t blocks = (required + 3U) / 4U;
    size_t password = ctx->password_length;
    size_t ssid = (size_t)ctx->total - password - 1U;
    size_t decoded_length = password;
    uint8_t plain[MAX_CIPHER] = {0};
    if (ctx->crypto_failed) return;
    for (i = 0; i < blocks; ++i) if (!ctx->seen[i]) return;
    if (recovered) {
        size_t block;
        for (block = 0; block < MAX_BLOCKS; ++block) {
            size_t j;
            for (j = 0; j < ctx->seen[block]; ++j) {
                size_t offset = block * 4U + j;
                if (offset >= required && offset < ctx->total &&
                    ctx->bytes[offset] != ctx->ap.ssid[offset - required]) return;
            }
        }
    }
    if (!recovered && sc_touch_crc8(ctx->bytes + password + 1U, ssid) != ctx->ssid_crc) return;
    if (ctx->config.key_len != 0 && password != 0) {
        uint8_t padding;
        if (ctx->config.decrypt(ctx->config.user, ctx->config.key, ctx->config.key,
                                ctx->bytes, password, plain) != 1) goto crypto_failure;
        padding = plain[password - 1U];
        if (padding == 0 || padding > 16 || padding > password) goto crypto_failure;
        decoded_length = password - padding;
        /* Strict observed-sender policy: an empty password has no ciphertext. */
        if (decoded_length == 0 || decoded_length > sizeof(ctx->result.password)) goto crypto_failure;
        for (i = decoded_length; i < password; ++i)
            if (plain[i] != padding) goto crypto_failure;
        memcpy(ctx->result.password, plain, decoded_length);
    } else memcpy(ctx->result.password, ctx->bytes, password);
    memcpy(ctx->result.ssid, recovered ? ctx->ap.ssid : ctx->bytes + password + 1U, ssid);
    ctx->result.password_len = (uint8_t)decoded_length;
    ctx->result.ssid_len = (uint8_t)ssid;
    ctx->result.token = ctx->bytes[password];
    ctx->complete = 1;
    clear_bytes(plain, sizeof(plain));
    return;
crypto_failure:
    ctx->crypto_failed = 1;
    clear_bytes(plain, sizeof(plain));
}

static void read_data(sc_airkiss *ctx, uint16_t length)
{
    size_t offset, count, full_count, required;
    uint8_t crc_input[5];
    if (length < 128 || length > 511 || !ctx->have_prefix) {
        ctx->header_count = 0;
        ctx->payload_count = 0;
        return;
    }
    if (length < 256) {
        uint8_t low = (uint8_t)(length & 127U);
        if (ctx->header_count == 0 || ctx->payload_count != 0) {
            ctx->block_crc = low;
            ctx->header_count = 1;
        } else if (ctx->header_count == 1) {
            ctx->block_index = low;
            ctx->header_count = 2;
        } else {
            /* Sliding pair recovers from an extra or missing header symbol. */
            ctx->block_crc = ctx->block_index;
            ctx->block_index = low;
        }
        ctx->payload_count = 0;
        return;
    }
    if (ctx->header_count != 2) return;
    offset = 4U * ctx->block_index;
    if (offset >= ctx->total) { ctx->header_count = 0; return; }
    full_count = (size_t)ctx->total - offset;
    if (full_count > 4) full_count = 4;
    required = recover_ssid(ctx) ? (size_t)ctx->password_length + 1U : ctx->total;
    count = offset < required ? required - offset : full_count;
    if (count > full_count) count = full_count;
    ctx->payload[ctx->payload_count++] = (uint8_t)(length & 255U);
    if (ctx->payload_count != count && ctx->payload_count != full_count) return;
    count = ctx->payload_count;
    crc_input[0] = ctx->block_index;
    memcpy(crc_input + 1, ctx->payload, count);
    if ((sc_touch_crc8(crc_input, count + 1U) & 127U) != ctx->block_crc) {
        if (count < full_count) return;
        ctx->header_count = 0; ctx->payload_count = 0; return;
    }
    ctx->header_count = 0; ctx->payload_count = 0;
    if (recover_ssid(ctx)) {
        size_t i;
        for (i = 0; i < count; ++i) if (offset + i >= required &&
            ctx->payload[i] != ctx->ap.ssid[offset + i - required]) { ctx->poisoned = 1; return; }
    }
    if (ctx->seen[ctx->block_index]) {
        if (memcmp(ctx->bytes + offset, ctx->payload, count) != 0) ctx->poisoned = 1;
        return;
    }
    memcpy(ctx->bytes + offset, ctx->payload, count);
    ctx->seen[ctx->block_index] = (uint8_t)count;
    try_complete(ctx);
}

int sc_airkiss_feed(sc_airkiss *ctx, uint16_t normalized_length)
{
    if (ctx == NULL) return -1;
    if (ctx->complete) return 1;
    if (ctx->poisoned) return -2;
    read_metadata(ctx, normalized_length);
    if (!ctx->poisoned) read_data(ctx, normalized_length);
    return ctx->poisoned ? -2 : ctx->complete;
}
int sc_airkiss_get_result(const sc_airkiss *ctx, sc_airkiss_result *out)
{
    if (ctx == NULL || out == NULL || !ctx->complete) return 0;
    *out = ctx->result;
    return 1;
}
int sc_airkiss_make_ack(const sc_airkiss_result *result, uint8_t out[1])
{
    if (result == NULL || out == NULL || result->ssid_len > 32 || result->password_len > 64)
        return 0;
    out[0] = result->token;
    return 1;
}
