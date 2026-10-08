/* Preserved diagnostic, now a regression derived solely from the contract. */
#include "sc_touch2_capture.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
static int feed_one(sc_touch2_capture *ctx, uint16_t length, uint16_t number, uint32_t time)
{
    uint8_t h[26] = {0};
    uint16_t sequence = (uint16_t)((number & 4095U) << 4);
    h[0] = 8; h[1] = 2;
    memset(h + 4, 255, 6); h[10] = 2; h[16] = 4;
    h[22] = (uint8_t)sequence; h[23] = (uint8_t)(sequence >> 8);
    return sc_touch2_capture_feed(ctx, h, 24, length, 6, time);
}
static int run(const uint16_t *wire_lengths, size_t count, int duplicate)
{
    sc_touch2_capture *ctx = sc_touch2_capture_create(NULL);
    sc_capture_lock lock;
    size_t i;
    int state = -1;
    CHECK(ctx != NULL);
    for (i = 0; i < count; ++i) {
        state = feed_one(ctx, wire_lengths[i], (uint16_t)i, (uint32_t)(i * 20U));
        if (duplicate) CHECK(feed_one(ctx, wire_lengths[i], (uint16_t)i, (uint32_t)(i * 20U + 1U)) == state);
    }
    if (state == 1) CHECK(sc_touch2_capture_get_lock(ctx, &lock) == 1 && lock.overhead == 80);
    sc_touch2_capture_destroy(ctx);
    return state;
}
int main(void)
{
    const uint16_t guide[] = {1128, 1159, 1128, 1159};
    /* 1100 = a valid plane-7 packet (1020) plus overhead 80. */
    const uint16_t prefixed[] = {1100, 1128, 1159, 1128, 1159};
    unsigned int prefix;
    size_t drop, i;
    CHECK(run(guide, sizeof(guide) / sizeof(guide[0]), 0) == 1);
    CHECK(run(prefixed, sizeof(prefixed) / sizeof(prefixed[0]), 0) == 1);
    CHECK(run(prefixed, sizeof(prefixed) / sizeof(prefixed[0]), 1) == 1);
    for (prefix = 0; prefix <= UINT16_MAX; ++prefix) {
        uint16_t lengths[] = {(uint16_t)prefix, 1128, 1159, 1128, 1159};
        CHECK(run(lengths, sizeof(lengths) / sizeof(lengths[0]), 0) == 1);
    }
    for (drop = 0; drop < 4; ++drop) {
        uint16_t lengths[8]; size_t used = 0;
        for (i = 0; i < 4; ++i) if (i != drop) lengths[used++] = guide[i];
        CHECK(run(lengths, used, 1) == 0);
        lengths[used++] = 144; /* An intervening data plane. */
        for (i = 0; i < 4; ++i) lengths[used++] = guide[i];
        CHECK(run(lengths, used, 1) == 1);
    }
    {
        sc_touch2_capture *ctx = sc_touch2_capture_create(NULL);
        CHECK(ctx != NULL);
        CHECK(feed_one(ctx, 1128, 0, 0) == 0);
        CHECK(feed_one(ctx, 1159, 0, 20) == 0); /* Same sequence is ignored, even a changed length. */
        CHECK(feed_one(ctx, 1128, 1, 40) == 0);
        CHECK(feed_one(ctx, 1159, 2, 60) == 0);
        CHECK(feed_one(ctx, 1128, 3, 80) == 0);
        CHECK(feed_one(ctx, 1159, 4, 100) == 1);
        sc_touch2_capture_destroy(ctx);
    }
    puts("PASS: v2 overlapping guide, all 65536 single-length prefixes, lost guide recovery and duplicate sequences");
    return 0;
}
