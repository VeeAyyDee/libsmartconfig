#include "sc_airkiss.h"
#include "sc_touch.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_TOTAL = 97, MAX_BLOCKS = 25 };
struct sc_airkiss {
    uint8_t bytes[MAX_TOTAL], seen[MAX_BLOCKS];
    uint16_t metadata[4];
    size_t metadata_count, payload_count;
    unsigned int header_count;
    uint8_t block_crc, block_index, payload[4];
    uint8_t total, ssid_crc, password_length;
    int have_magic, have_prefix, poisoned, complete;
    sc_airkiss_result result;
};

sc_airkiss *sc_airkiss_create(void) { return calloc(1, sizeof(sc_airkiss)); }
void sc_airkiss_reset(sc_airkiss *ctx)
{ if (ctx != NULL) memset(ctx, 0, sizeof(*ctx)); }
void sc_airkiss_destroy(sc_airkiss *ctx)
{ if (ctx != NULL) { sc_airkiss_reset(ctx); free(ctx); } }

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
        else if (high < 1 || high > 6) return;
        total = (high << 4) | (ctx->metadata[1] & 15U);
        checksum = ((ctx->metadata[2] & 15U) << 4) | (ctx->metadata[3] & 15U);
        if (total < 1 || total > MAX_TOTAL) return;
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
        if (password > 64 || password >= ctx->total ||
            ctx->total - password - 1U > 32U ||
            sc_touch_crc8(&password_byte, 1) != checksum) return;
        if (ctx->have_prefix && ctx->password_length != password) ctx->poisoned = 1;
        else { ctx->password_length = password_byte; ctx->have_prefix = 1; }
    }
}

static void try_complete(sc_airkiss *ctx)
{
    size_t i, blocks = ((size_t)ctx->total + 3U) / 4U;
    size_t password = ctx->password_length;
    size_t ssid = (size_t)ctx->total - password - 1U;
    for (i = 0; i < blocks; ++i) if (!ctx->seen[i]) return;
    if (sc_touch_crc8(ctx->bytes + password + 1U, ssid) != ctx->ssid_crc) return;
    memcpy(ctx->result.password, ctx->bytes, password);
    memcpy(ctx->result.ssid, ctx->bytes + password + 1U, ssid);
    ctx->result.password_len = (uint8_t)password;
    ctx->result.ssid_len = (uint8_t)ssid;
    ctx->result.token = ctx->bytes[password];
    ctx->complete = 1;
}

static void read_data(sc_airkiss *ctx, uint16_t length)
{
    size_t offset, count;
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
    count = (size_t)ctx->total - offset;
    if (count > 4) count = 4;
    ctx->payload[ctx->payload_count++] = (uint8_t)(length & 255U);
    if (ctx->payload_count != count) return;
    ctx->header_count = 0;
    ctx->payload_count = 0;
    crc_input[0] = ctx->block_index;
    memcpy(crc_input + 1, ctx->payload, count);
    if ((sc_touch_crc8(crc_input, count + 1U) & 127U) != ctx->block_crc) return;
    if (ctx->seen[ctx->block_index]) {
        if (memcmp(ctx->bytes + offset, ctx->payload, count) != 0) ctx->poisoned = 1;
        return;
    }
    memcpy(ctx->bytes + offset, ctx->payload, count);
    ctx->seen[ctx->block_index] = 1;
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
