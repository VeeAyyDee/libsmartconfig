#include "sc_touch2.h"
#include "sc_touch.h"
#include <stdlib.h>
#include <string.h>

enum { MAX_GROUPS = 40, MAX_CIPHER = 144 };
typedef struct { size_t start, groups, width; } segment;
struct sc_touch2 {
    sc_scan_ap ap;
    sc_touch2_config config;
    sc_touch2_result result;
    uint8_t header[6], data[MAX_GROUPS][6], seen[MAX_GROUPS], widths[MAX_GROUPS];
    uint8_t planes[8], plane_mask, pending_count, header_count;
    unsigned int sync_phase;
    size_t selected_index, group_count, cipher_length;
    segment password, reserved, ssid, cipher, iv;
    int selected, selected_header, have_header, joined, complete, poisoned;
};

sc_touch2 *sc_touch2_create(const sc_touch2_config *config)
{
    sc_touch2 *ctx = calloc(1, sizeof(*ctx));
    if (ctx != NULL && config != NULL) ctx->config = *config;
    return ctx;
}
void sc_touch2_set_ap(sc_touch2 *ctx, const sc_scan_ap *ap)
{ if (ctx != NULL && !ctx->complete) { memset(&ctx->ap, 0, sizeof(ctx->ap)); if (ap != NULL && ap->ssid_len <= 32) ctx->ap = *ap; } }

void sc_touch2_reset(sc_touch2 *ctx)
{
    sc_touch2_config config;
    if (ctx == NULL) return;
    config = ctx->config;
    memset(ctx, 0, sizeof(*ctx));
    ctx->config = config;
}
void sc_touch2_destroy(sc_touch2 *ctx)
{ if (ctx != NULL) { memset(ctx, 0, sizeof(*ctx)); free(ctx); } }

static void clear_planes(sc_touch2 *ctx)
{
    ctx->plane_mask = 0;
    ctx->selected = 0;
}
static int add_segment(sc_touch2 *ctx, segment *part, size_t bytes, size_t width)
{
    size_t i, groups = (bytes + width - 1U) / width;
    if (groups > MAX_GROUPS - ctx->group_count) return 0;
    part->start = ctx->group_count; part->groups = groups; part->width = width;
    for (i = 0; i < groups; ++i) ctx->widths[ctx->group_count++] = (uint8_t)width;
    return 1;
}
static int set_layout(sc_touch2 *ctx, const uint8_t h[6])
{
    size_t ssid = h[0] & 127U, password = h[1] & 127U, reserved = h[2] & 127U;
    unsigned int security = (h[4] >> 1) & 3U;
    if (ssid > 32 || password > 64 || reserved > 64 ||
        (h[4] & 224U) != 0U || security == 3U) return 0;
    ctx->group_count = 0;
    ctx->joined = security == 0 && (h[1] & 128U) == 0 && (h[2] & 128U) == 0;
    if (security != 0) {
        ctx->cipher_length = 16U * ((password + reserved) / 16U + 1U);
        if (ctx->cipher_length > MAX_CIPHER ||
            !add_segment(ctx, &ctx->cipher, ctx->cipher_length, 5)) return 0;
        if (security == 2 && !add_segment(ctx, &ctx->iv, 20, 5)) return 0;
    } else if (ctx->joined) {
        if (!add_segment(ctx, &ctx->password, password + reserved, 6)) return 0;
    } else {
        if (!add_segment(ctx, &ctx->password, password, (h[1] & 128U) != 0U ? 5 : 6) ||
            !add_segment(ctx, &ctx->reserved, reserved, (h[2] & 128U) != 0U ? 5 : 6)) return 0;
    }
    return add_segment(ctx, &ctx->ssid, ssid, (h[0] & 128U) != 0U ? 5 : 6);
}
static int gather(const sc_touch2 *ctx, const segment *part, uint8_t *out,
                   size_t wanted, size_t capacity)
{
    size_t i, written = 0;
    if (wanted > capacity || part->start > ctx->group_count ||
        part->groups > ctx->group_count - part->start ||
        wanted > part->groups * part->width) return 0;
    for (i = 0; i < part->groups && written < wanted; ++i) {
        size_t count = wanted - written;
        if (count > part->width) count = part->width;
        memcpy(out + written, ctx->data[part->start + i], count);
        written += count;
    }
    return written == wanted;
}
static void try_complete(sc_touch2 *ctx)
{
    sc_touch2_result result;
    uint8_t cipher[MAX_CIPHER], plain[MAX_CIPHER], iv[16] = {0};
    size_t i, password, reserved, plain_length, padding;
    unsigned int security;
    int recovered;
    if (!ctx->have_header) return;
    recovered = ctx->ap.ssid_len != 0 && ctx->ap.ssid_len == (ctx->header[0] & 127U) &&
        sc_touch_crc8(ctx->ap.bssid, 6) == ctx->header[3];
    for (i = 0; i < ctx->group_count; ++i) {
        if (recovered && i >= ctx->ssid.start && i < ctx->ssid.start + ctx->ssid.groups) {
            size_t offset = (i - ctx->ssid.start) * ctx->ssid.width;
            size_t n = ctx->ap.ssid_len - offset;
            if (n > ctx->ssid.width) n = ctx->ssid.width;
            if (ctx->seen[i] && memcmp(ctx->data[i], ctx->ap.ssid + offset, n) != 0) return;
        } else if (!ctx->seen[i]) return;
    }
    memset(&result, 0, sizeof(result));
    result.ssid_len = (uint8_t)(ctx->header[0] & 127U);
    result.password_len = (uint8_t)(ctx->header[1] & 127U);
    result.reserved_len = (uint8_t)(ctx->header[2] & 127U);
    result.bssid_crc = ctx->header[3];
    result.ipv4 = (uint8_t)(ctx->header[4] & 1U);
    result.port_mark = (uint8_t)((ctx->header[4] >> 3) & 3U);
    result.security_version = (uint8_t)((ctx->header[4] >> 1) & 3U);
    password = result.password_len; reserved = result.reserved_len;
    plain_length = password + reserved; security = result.security_version;
    if (security != 0) {
        if (ctx->config.decrypt == NULL ||
            !gather(ctx, &ctx->cipher, cipher, ctx->cipher_length, sizeof(cipher)) ||
            (security == 2 && !gather(ctx, &ctx->iv, iv, sizeof(iv), sizeof(iv)))) return;
        memset(plain, 0, sizeof(plain));
        if (ctx->config.decrypt(ctx->config.user, ctx->config.key, iv, cipher,
                                ctx->cipher_length, plain) != 1) return;
        padding = ctx->cipher_length - plain_length;
        if (padding < 1 || padding > 16) return;
        for (i = plain_length; i < ctx->cipher_length; ++i)
            if (plain[i] != padding) return;
        memcpy(result.password, plain, password);
        memcpy(result.reserved, plain + password, reserved);
    } else if (ctx->joined) {
        if (!gather(ctx, &ctx->password, plain, plain_length, sizeof(plain))) return;
        memcpy(result.password, plain, password);
        memcpy(result.reserved, plain + password, reserved);
    } else if (!gather(ctx, &ctx->password, result.password, password, sizeof(result.password)) ||
               !gather(ctx, &ctx->reserved, result.reserved, reserved, sizeof(result.reserved))) return;
    if (recovered) memcpy(result.ssid, ctx->ap.ssid, result.ssid_len);
    else if (!gather(ctx, &ctx->ssid, result.ssid, result.ssid_len, sizeof(result.ssid))) return;
    ctx->result = result;
    ctx->complete = 1;
}
static void commit_planes(sc_touch2 *ctx)
{
    uint8_t decoded[6] = {0};
    size_t i, k, bits = 8;
    if (!ctx->selected_header && ctx->widths[ctx->selected_index] == 6) bits = 7;
    for (i = 0; i < bits; ++i) for (k = 0; k < 6; ++k)
        decoded[k] |= (uint8_t)((((unsigned int)ctx->planes[i] >> (5U - k)) & 1U) << i);
    if (ctx->selected_header) {
        if (sc_touch_crc8(decoded, 5) != decoded[5]) return;
        if (ctx->have_header) {
            if (memcmp(ctx->header, decoded, 6) != 0) ctx->poisoned = 1;
        } else {
            if (!set_layout(ctx, decoded) || ctx->group_count + 1U != ctx->pending_count) return;
            memcpy(ctx->header, decoded, 6);
            ctx->header_count = ctx->pending_count;
            ctx->have_header = 1;
        }
    } else {
        size_t index = ctx->selected_index;
        if ((bits == 7 && (sc_touch_crc8(decoded, 6) & 63U) != ctx->planes[7]) ||
            (bits == 8 && sc_touch_crc8(decoded, 5) != decoded[5])) return;
        if (ctx->seen[index]) {
            if (memcmp(ctx->data[index], decoded, 6) != 0) ctx->poisoned = 1;
        } else { memcpy(ctx->data[index], decoded, 6); ctx->seen[index] = 1; }
    }
    if (!ctx->poisoned) try_complete(ctx);
}
int sc_touch2_feed(sc_touch2 *ctx, uint16_t length)
{
    if (ctx == NULL) return -1;
    if (ctx->complete) return 1;
    if (ctx->poisoned) return -2;
    if (length == 1048) {
        clear_planes(ctx);
        ctx->sync_phase = ctx->sync_phase == 2 ? 3U : 1U;
    } else if (length >= 1072 && length <= 1112) {
        uint8_t count = (uint8_t)(length - 1071U);
        clear_planes(ctx);
        if (ctx->have_header && count != ctx->header_count) ctx->poisoned = 1;
        else if (ctx->sync_phase == 1) { ctx->pending_count = count; ctx->sync_phase = 2; }
        else if (ctx->sync_phase == 3 && count == ctx->pending_count) {
            ctx->selected = 1; ctx->selected_header = 1; ctx->sync_phase = 0;
        } else ctx->sync_phase = 0;
    } else if (length >= 128 && length <= 167) {
        clear_planes(ctx); ctx->sync_phase = 0;
        if (ctx->have_header && (size_t)(length - 128U) < ctx->group_count) {
            ctx->selected = 1; ctx->selected_header = 0; ctx->selected_index = length - 128U;
        }
    } else if (length >= 64 && length <= 1023 && (length & 64U) != 0U) {
        size_t index = (length >> 7) & 7U;
        uint8_t data = (uint8_t)(length & 63U), bit = (uint8_t)(1U << index);
        ctx->sync_phase = 0;
        if (ctx->selected) {
            if ((ctx->plane_mask & bit) != 0U && ctx->planes[index] != data) clear_planes(ctx);
            else {
                ctx->planes[index] = data; ctx->plane_mask |= bit;
                if (ctx->plane_mask == 255) { commit_planes(ctx); clear_planes(ctx); }
            }
        }
    } else { clear_planes(ctx); ctx->sync_phase = 0; }
    return ctx->poisoned ? -2 : ctx->complete;
}
int sc_touch2_get_result(const sc_touch2 *ctx, sc_touch2_result *out)
{
    if (ctx == NULL || out == NULL || !ctx->complete) return 0;
    *out = ctx->result; return 1;
}
int sc_touch2_make_ack(const sc_touch2_result *result, const uint8_t mac[6],
                        uint8_t out[7], uint16_t *port)
{
    uint8_t bytes[7];
    uint16_t destination;
    if (result == NULL || mac == NULL || out == NULL || port == NULL ||
        result->ssid_len > 32 || result->password_len > 64 || result->reserved_len > 64 ||
        result->port_mark > 3 || result->security_version > 2) return 0;
    bytes[0] = result->port_mark; memcpy(bytes + 1, mac, 6);
    destination = (uint16_t)(18266U + 10000U * result->port_mark);
    memcpy(out, bytes, sizeof(bytes)); *port = destination; return 1;
}
