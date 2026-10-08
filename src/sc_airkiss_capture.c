#include "sc_airkiss_capture.h"
#include <stdlib.h>
#include <string.h>

enum { CANDIDATES = 4, CANDIDATE_MS = 1500, IDLE_MS = 2500, SESSION_MS = 30000 };
typedef struct {
    uint8_t bssid[6], sender[6], channel, direction, qos, protected_frame;
} frame_key;
typedef struct {
    frame_key key;
    uint32_t updated;
    uint16_t first_length, sequence;
    unsigned int progress;
    int used;
} guide_candidate;
struct sc_airkiss_capture {
    sc_airkiss *decoder;
    guide_candidate candidates[CANDIDATES];
    frame_key selected;
    sc_capture_lock lock;
    sc_airkiss_result result;
    uint32_t locked_at, last_frame;
    uint16_t sequence;
    int state;
};

static int unicast_nonzero(const uint8_t *address)
{
    size_t i;
    unsigned int any = 0;
    if ((address[0] & 1U) != 0U) return 0;
    for (i = 0; i < 6; ++i) any |= address[i];
    return any != 0U;
}

static int parse_header(const uint8_t *header, size_t available,
                        uint16_t wire_length, uint8_t channel,
                        frame_key *key, uint16_t *sequence)
{
    unsigned int fc, subtype, direction;
    size_t required;
    const uint8_t *bssid, *sender, *destination;
    if (header == NULL || available < 24 || channel < 1 || channel > 14 ||
        (size_t)wire_length < available) return 0;
    fc = (unsigned int)header[0] | ((unsigned int)header[1] << 8);
    subtype = (fc >> 4) & 15U;
    direction = (fc >> 8) & 3U;
    if ((fc & 3U) != 0U || ((fc >> 2) & 3U) != 2U ||
        (subtype != 0U && subtype != 8U) ||
        (direction != 1U && direction != 2U) || (fc & 0x8400U) != 0U ||
        (header[22] & 15U) != 0U) return 0;
    required = subtype == 8U ? 26U : 24U;
    if (available < required || wire_length < required + 4U ||
        (subtype == 8U && (header[24] & 128U) != 0U)) return 0;
    bssid = header + (direction == 1U ? 4 : 10);
    sender = header + (direction == 1U ? 10 : 16);
    destination = header + (direction == 1U ? 16 : 4);
    if ((destination[0] & 1U) == 0U || !unicast_nonzero(bssid) ||
        !unicast_nonzero(sender)) return 0;
    memcpy(key->bssid, bssid, 6);
    memcpy(key->sender, sender, 6);
    key->channel = channel;
    key->direction = (uint8_t)direction;
    key->qos = (uint8_t)(subtype == 8U);
    key->protected_frame = (uint8_t)((fc >> 14) & 1U);
    *sequence = (uint16_t)((unsigned int)header[22] | ((unsigned int)header[23] << 8));
    return 1;
}

static int same_key(const frame_key *a, const frame_key *b)
{
    return memcmp(a->bssid, b->bssid, 6) == 0 &&
           memcmp(a->sender, b->sender, 6) == 0 &&
           a->channel == b->channel && a->direction == b->direction &&
           a->qos == b->qos && a->protected_frame == b->protected_frame;
}

sc_airkiss_capture *sc_airkiss_capture_create(void)
{
    return sc_airkiss_capture_create_with_config(NULL);
}

sc_airkiss_capture *sc_airkiss_capture_create_with_config(const sc_airkiss_config *config)
{
    sc_airkiss_capture *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) return NULL;
    ctx->decoder = sc_airkiss_create_with_config(config);
    if (ctx->decoder == NULL) { free(ctx); return NULL; }
    return ctx;
}

void sc_airkiss_capture_reset(sc_airkiss_capture *ctx)
{
    sc_airkiss *decoder;
    if (ctx == NULL) return;
    decoder = ctx->decoder;
    sc_airkiss_reset(decoder);
    memset(ctx, 0, sizeof(*ctx));
    ctx->decoder = decoder;
}

void sc_airkiss_capture_destroy(sc_airkiss_capture *ctx)
{
    if (ctx == NULL) return;
    sc_airkiss_destroy(ctx->decoder);
    memset(ctx, 0, sizeof(*ctx));
    free(ctx);
}

int sc_airkiss_capture_tick(sc_airkiss_capture *ctx, uint32_t now_ms)
{
    size_t i;
    if (ctx == NULL) return -1;
    if (ctx->state == 2) return 2;
    if (ctx->state == 1 &&
        ((uint32_t)(now_ms - ctx->last_frame) >= IDLE_MS ||
         (uint32_t)(now_ms - ctx->locked_at) >= SESSION_MS)) {
        sc_airkiss_capture_reset(ctx);
    }
    for (i = 0; i < CANDIDATES; ++i) {
        if (ctx->candidates[i].used != 0 &&
            (uint32_t)(now_ms - ctx->candidates[i].updated) >= CANDIDATE_MS) {
            memset(&ctx->candidates[i], 0, sizeof(ctx->candidates[i]));
        }
    }
    return ctx->state;
}

static void search_guide(sc_airkiss_capture *ctx, const frame_key *key,
                         uint16_t sequence, uint16_t length, uint32_t now_ms)
{
    guide_candidate *candidate = NULL;
    size_t i, replacement = 0;
    uint32_t oldest = 0;
    for (i = 0; i < CANDIDATES; ++i) {
        if (ctx->candidates[i].used != 0 && same_key(&ctx->candidates[i].key, key)) {
            candidate = &ctx->candidates[i];
            break;
        }
    }
    if (candidate == NULL) {
        /* New sequences must start with a possible 1+overhead guide. */
        if (length < 1 || length > 257) return;
        for (i = 0; i < CANDIDATES; ++i) {
            uint32_t age = now_ms - ctx->candidates[i].updated;
            if (ctx->candidates[i].used == 0) { replacement = i; break; }
            if (age > oldest) { oldest = age; replacement = i; }
        }
        candidate = &ctx->candidates[replacement];
        memset(candidate, 0, sizeof(*candidate));
        candidate->key = *key;
        candidate->used = 1;
    } else if (candidate->sequence == sequence) {
        return;
    }
    candidate->sequence = sequence;
    candidate->updated = now_ms;
    if (candidate->progress != 0 &&
        length == candidate->first_length + (candidate->progress % 4U)) {
        ++candidate->progress;
    } else {
        candidate->progress = (length >= 1 && length <= 257) ? 1U : 0U;
        candidate->first_length = length;
    }
    if (candidate->progress == 8) {
        ctx->selected = *key;
        memcpy(ctx->lock.bssid, key->bssid, 6);
        memcpy(ctx->lock.sender, key->sender, 6);
        ctx->lock.channel = key->channel;
        ctx->lock.overhead = (uint16_t)(candidate->first_length - 1U);
        ctx->sequence = sequence;
        ctx->last_frame = now_ms;
        ctx->locked_at = now_ms;
        ctx->state = 1;
        memset(ctx->candidates, 0, sizeof(ctx->candidates));
    }
}

int sc_airkiss_capture_feed(sc_airkiss_capture *ctx, const uint8_t *header,
                    size_t header_bytes, uint16_t wire_length,
                    uint8_t channel, uint32_t now_ms)
{
    frame_key key;
    uint16_t sequence;
    int decoded;
    if (ctx == NULL) return -1;
    if (ctx->state == 2) return 2;
    if (!parse_header(header, header_bytes, wire_length, channel, &key, &sequence))
        return ctx->state;
    (void)sc_airkiss_capture_tick(ctx, now_ms);
    if (ctx->state == 0) {
        search_guide(ctx, &key, sequence, wire_length, now_ms);
        return ctx->state;
    }
    if (!same_key(&ctx->selected, &key) || sequence == ctx->sequence ||
        wire_length < ctx->lock.overhead) return ctx->state;
    ctx->sequence = sequence;
    ctx->last_frame = now_ms;
    decoded = sc_airkiss_feed(ctx->decoder, (uint16_t)(wire_length - ctx->lock.overhead));
    if (decoded == -2) {
        sc_airkiss_capture_reset(ctx);
    } else if (decoded == 1) {
        (void)sc_airkiss_get_result(ctx->decoder, &ctx->result);
        ctx->state = 2;
    }
    return ctx->state;
}

int sc_airkiss_capture_get_state(const sc_airkiss_capture *ctx)
{
    return ctx == NULL ? -1 : ctx->state;
}
int sc_airkiss_capture_get_lock(const sc_airkiss_capture *ctx, sc_capture_lock *out)
{
    if (ctx == NULL || out == NULL || ctx->state == 0) return 0;
    *out = ctx->lock;
    return 1;
}
int sc_airkiss_capture_get_result(const sc_airkiss_capture *ctx, sc_airkiss_result *out)
{
    if (ctx == NULL || out == NULL || ctx->state != 2) return 0;
    *out = ctx->result;
    return 1;
}
