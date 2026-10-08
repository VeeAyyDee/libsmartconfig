#include "sc_touch.h"

#include <stdlib.h>
#include <string.h>

enum {
    SC_INDEX_COUNT = 128,
    SC_LENGTH_OFFSET = 40,
    SC_DATA_SYMBOL_MAX = 255,
    SC_INDEX_SYMBOL_MIN = 256,
    SC_INDEX_SYMBOL_MAX = 383,
    SC_HEADER_LENGTH = 9,
    SC_TOTAL_MAX = 105,
    SC_BSSID_LENGTH = 6
};

struct sc_touch {
    uint8_t bytes[SC_INDEX_COUNT];
    uint8_t seen[SC_INDEX_COUNT];
    uint16_t window[3];
    size_t window_count;
    int poisoned;
    int complete;
    sc_touch_result result;
};

uint8_t sc_touch_crc8(const uint8_t *bytes, size_t length)
{
    uint8_t crc = 0;
    size_t i;
    unsigned int bit;

    if (bytes == NULL) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        crc = (uint8_t)(crc ^ bytes[i]);
        for (bit = 0; bit < 8; ++bit) {
            if ((crc & 1U) != 0U) {
                crc = (uint8_t)((crc >> 1) ^ 0x8cU);
            } else {
                crc = (uint8_t)(crc >> 1);
            }
        }
    }
    return crc;
}

int sc_touch_decode_triplet(const uint16_t lengths[3],
                            uint8_t *index, uint8_t *value)
{
    unsigned int a;
    unsigned int b;
    unsigned int c;
    uint8_t data_and_index[2];
    uint8_t checksum;

    if (lengths == NULL || index == NULL || value == NULL) {
        return 0;
    }
    if (lengths[0] < SC_LENGTH_OFFSET ||
        lengths[0] > SC_LENGTH_OFFSET + SC_DATA_SYMBOL_MAX ||
        lengths[1] < SC_LENGTH_OFFSET + SC_INDEX_SYMBOL_MIN ||
        lengths[1] > SC_LENGTH_OFFSET + SC_INDEX_SYMBOL_MAX ||
        lengths[2] < SC_LENGTH_OFFSET ||
        lengths[2] > SC_LENGTH_OFFSET + SC_DATA_SYMBOL_MAX) {
        return 0;
    }

    a = (unsigned int)lengths[0] - SC_LENGTH_OFFSET;
    b = (unsigned int)lengths[1] - SC_LENGTH_OFFSET;
    c = (unsigned int)lengths[2] - SC_LENGTH_OFFSET;
    data_and_index[0] = (uint8_t)(((a & 15U) << 4) | (c & 15U));
    data_and_index[1] = (uint8_t)(b - SC_INDEX_SYMBOL_MIN);
    checksum = (uint8_t)((a & 240U) | (c >> 4));
    if (sc_touch_crc8(data_and_index, sizeof(data_and_index)) != checksum) {
        return 0;
    }
    *index = data_and_index[1];
    *value = data_and_index[0];
    return 1;
}

sc_touch *sc_touch_create(void)
{
    return calloc(1, sizeof(sc_touch));
}

void sc_touch_reset(sc_touch *ctx)
{
    if (ctx != NULL) {
        memset(ctx, 0, sizeof(*ctx));
    }
}

void sc_touch_destroy(sc_touch *ctx)
{
    if (ctx != NULL) {
        sc_touch_reset(ctx);
        free(ctx);
    }
}

static int try_complete(sc_touch *ctx)
{
    size_t total;
    size_t password_length;
    size_t ssid_length;
    size_t ssid_start;
    size_t i;
    uint8_t message_xor = 0;

    if (ctx->seen[0] == 0 || ctx->seen[1] == 0) {
        return 0;
    }
    total = ctx->bytes[0];
    password_length = ctx->bytes[1];
    if (total < SC_HEADER_LENGTH || total > SC_TOTAL_MAX ||
        password_length > sizeof(ctx->result.password) ||
        password_length > total - SC_HEADER_LENGTH) {
        return 0;
    }
    ssid_start = SC_HEADER_LENGTH + password_length;
    ssid_length = total - ssid_start;
    if (ssid_length > sizeof(ctx->result.ssid)) {
        return 0;
    }
    for (i = 0; i < total + SC_BSSID_LENGTH; ++i) {
        if (ctx->seen[i] == 0) {
            return 0;
        }
    }
    for (i = 0; i < total; ++i) {
        if (i != 4) {
            message_xor = (uint8_t)(message_xor ^ ctx->bytes[i]);
        }
    }
    if (message_xor != ctx->bytes[4] ||
        sc_touch_crc8(ctx->bytes + ssid_start, ssid_length) != ctx->bytes[2] ||
        sc_touch_crc8(ctx->bytes + total, SC_BSSID_LENGTH) != ctx->bytes[3]) {
        return 0;
    }

    memcpy(ctx->result.ssid, ctx->bytes + ssid_start, ssid_length);
    memcpy(ctx->result.password, ctx->bytes + SC_HEADER_LENGTH, password_length);
    memcpy(ctx->result.bssid, ctx->bytes + total, SC_BSSID_LENGTH);
    memcpy(ctx->result.sender_ip, ctx->bytes + 5, sizeof(ctx->result.sender_ip));
    ctx->result.ssid_len = (uint8_t)ssid_length;
    ctx->result.password_len = (uint8_t)password_length;
    ctx->complete = 1;
    return 1;
}

int sc_touch_feed(sc_touch *ctx, uint16_t udp_payload_length)
{
    uint8_t index;
    uint8_t value;

    if (ctx == NULL) {
        return -1;
    }
    if (ctx->complete != 0) {
        return 1;
    }
    if (ctx->poisoned != 0) {
        return -2;
    }
    if (udp_payload_length < SC_LENGTH_OFFSET ||
        udp_payload_length > SC_LENGTH_OFFSET + SC_INDEX_SYMBOL_MAX) {
        ctx->window_count = 0;
        return 0;
    }

    if (ctx->window_count == 3) {
        ctx->window[0] = ctx->window[1];
        ctx->window[1] = ctx->window[2];
        ctx->window_count = 2;
    }
    ctx->window[ctx->window_count] = udp_payload_length;
    ++ctx->window_count;
    if (ctx->window_count != 3 ||
        sc_touch_decode_triplet(ctx->window, &index, &value) == 0) {
        return 0;
    }
    if (ctx->seen[index] != 0) {
        if (ctx->bytes[index] != value) {
            ctx->poisoned = 1;
            return -2;
        }
        return 0;
    }
    ctx->bytes[index] = value;
    ctx->seen[index] = 1;
    return try_complete(ctx);
}

int sc_touch_get_result(const sc_touch *ctx, sc_touch_result *out)
{
    if (ctx == NULL || out == NULL || ctx->complete == 0) {
        return 0;
    }
    *out = ctx->result;
    return 1;
}
