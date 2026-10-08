#include "sc_capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
static const uint8_t bssid[6] = {2, 3, 4, 5, 6, 7};
static const uint8_t sender[6] = {4, 3, 4, 5, 6, 8};
static uint16_t next_sequence;

static size_t header_make(uint8_t h[26], unsigned int direction,
                          unsigned int qos, unsigned int protected_frame)
{
    const uint8_t destination[6] = {255, 255, 255, 255, 255, 255};
    memset(h, 0, 26);
    h[0] = (uint8_t)(8U | (qos != 0 ? 128U : 0U));
    h[1] = (uint8_t)(direction | (protected_frame != 0 ? 64U : 0U));
    memcpy(h + (direction == 1 ? 4 : 10), bssid, 6);
    memcpy(h + (direction == 1 ? 10 : 16), sender, 6);
    memcpy(h + (direction == 1 ? 16 : 4), destination, 6);
    return qos != 0 ? 26 : 24;
}
static void set_sequence(uint8_t h[26])
{
    uint16_t seq = (uint16_t)((next_sequence++ & 4095U) << 4);
    h[22] = (uint8_t)seq;
    h[23] = (uint8_t)(seq >> 8);
}
static int feed(sc_capture *c, uint8_t h[26], size_t n, uint16_t length, uint32_t time)
{
    set_sequence(h);
    return sc_capture_feed(c, h, n, length, 6, time);
}
static void lock_context(sc_capture *c, uint8_t h[26], size_t n, uint16_t overhead, uint32_t time)
{
    unsigned int i;
    for (i = 0; i < 8; ++i)
        CHECK(feed(c, h, n, (uint16_t)(515U + overhead - i % 4U), time) == (i == 7 ? 1 : 0));
}
static void encode(uint8_t index, uint8_t value, uint16_t lengths[3])
{
    uint8_t pair[2] = {value, index};
    unsigned int crc = sc_touch_crc8(pair, 2);
    lengths[0] = (uint16_t)(40U + (crc & 240U) + (value >> 4));
    lengths[1] = (uint16_t)(296U + index);
    lengths[2] = (uint16_t)(40U + ((crc & 15U) << 4) + (value & 15U));
}
static int record(sc_capture *c, uint8_t h[26], size_t n, uint8_t index, uint8_t value, uint16_t overhead, uint32_t time)
{
    uint16_t lengths[3];
    size_t i;
    int state = 0;
    encode(index, value, lengths);
    for (i = 0; i < 3; ++i) state = feed(c, h, n, (uint16_t)(lengths[i] + overhead), time);
    return state;
}
static void test_acquisition(void)
{
    unsigned int overhead, phase, direction, qos, protected_frame;
    for (overhead = 0; overhead <= 256; ++overhead)
    for (phase = 0; phase < 4; ++phase)
    for (direction = 1; direction <= 2; ++direction)
    for (qos = 0; qos < 2; ++qos)
    for (protected_frame = 0; protected_frame < 2; ++protected_frame) {
        sc_capture *c = sc_capture_create();
        uint8_t h[26];
        size_t n = header_make(h, direction, qos, protected_frame);
        unsigned int i;
        sc_capture_lock lock;
        CHECK(c != NULL);
        for (i = phase; i < 12; ++i) {
            int state = feed(c, h, n, (uint16_t)(515U + overhead - i % 4U), 10U + i);
            if (state == 1) break;
            CHECK(state == 0);
        }
        CHECK(sc_capture_get_lock(c, &lock) == 1);
        CHECK(lock.overhead == overhead && lock.channel == 6);
        CHECK(memcmp(lock.bssid, bssid, 6) == 0);
        CHECK(memcmp(lock.sender, sender, 6) == 0);
        sc_capture_destroy(c);
    }
}
static void test_invalid_and_other_keys(void)
{
    sc_capture *c = sc_capture_create();
    uint8_t h[26], bad[26];
    size_t n = header_make(h, 1, 0, 1);
    unsigned int i;
    CHECK(c != NULL);
    CHECK(feed(c, h, n, 615, 0) == 0);
    for (i = 0; i < 17; ++i) {
        uint16_t wire = 614;
        size_t available = n;
        uint8_t channel = 6;
        memcpy(bad, h, 26);
        set_sequence(bad);
        switch (i) {
        case 0: bad[0] |= 1U; break;
        case 1: bad[0] = 0; break;
        case 2: bad[0] = 0x48; break;
        case 3: bad[1] &= 0xfcU; break;
        case 4: bad[1] |= 3U; break;
        case 5: bad[1] |= 4U; break;
        case 6: bad[1] |= 128U; break;
        case 7: bad[22] |= 1U; break;
        case 8: bad[16] = 2; break;
        case 9: bad[4] = 3; break;
        case 10: memset(bad + 10, 0, 6); break;
        case 11: available = 23; break;
        case 12: wire = 23; break;
        case 13: wire = 27; break;
        case 14: channel = 0; break;
        case 15: channel = 15; break;
        case 16: bad[0] = 0x88; available = 26; bad[24] = 128; break;
        default: abort();
        }
        CHECK(sc_capture_feed(c, bad, available, wire, channel, 10) == 0);
    }
    CHECK(sc_capture_feed(c, NULL, 26, 614, 6, 10) == 0);
    for (i = 1; i < 8; ++i) {
        /* Other source's unrelated lengths must not disturb acquisition. */
        memcpy(bad, h, 26); bad[10] = 6;
        CHECK(feed(c, bad, n, 621, 10) == 0);
        CHECK(feed(c, h, n, (uint16_t)(615U - i % 4U), 10) == (i == 7 ? 1 : 0));
    }
    sc_capture_destroy(c);
}
static void test_expiry_and_duplicates(void)
{
    sc_capture *c = sc_capture_create();
    uint8_t h[26];
    size_t n = header_make(h, 2, 1, 1);
    unsigned int i;
    uint32_t start = UINT32_MAX - 1000U;
    CHECK(c != NULL);
    CHECK(feed(c, h, n, 615, start) == 0);
    for (i = 0; i < 7; ++i)
        CHECK(sc_capture_feed(c, h, n, 614, 6, start + 100U) == 0);
    CHECK(sc_capture_tick(c, start + 1500U) == 0);
    for (i = 1; i < 8; ++i)
        CHECK(feed(c, h, n, (uint16_t)(615U - i % 4U), start + 1501U) == 0);
    sc_capture_reset(c);
    lock_context(c, h, n, 100, start);
    CHECK(sc_capture_feed(c, h, n, 612, 6, start + 2499U) == 1);
    CHECK(sc_capture_tick(c, start + 2500U) == 0);
    lock_context(c, h, n, 100, start);
    for (i = 1; i < 30; ++i)
        CHECK(feed(c, h, n, 615, start + i * 1000U) == 1);
    CHECK(sc_capture_tick(c, start + 30000U) == 0);
    lock_context(c, h, n, 100, 0);
    CHECK(feed(c, h, n, 50, 2499) == 1); /* Underflow is not activity. */
    CHECK(sc_capture_tick(c, 2500) == 0);
    sc_capture_destroy(c);
}
static void test_candidate_capacity(void)
{
    sc_capture *c = sc_capture_create();
    uint8_t h[5][26];
    size_t n, key;
    unsigned int step;
    sc_capture_lock lock;
    CHECK(c != NULL);
    for (key = 0; key < 5; ++key) {
        n = header_make(h[key], 1, 0, 0);
        h[key][10] = (uint8_t)(2U + key * 2U);
    }
    /* Four interleaved keys keep independent progress. */
    for (step = 0; step < 7; ++step)
        for (key = 0; key < 4; ++key)
            CHECK(feed(c, h[key], n, (uint16_t)(615U - step % 4U), step) == 0);
    CHECK(feed(c, h[3], n, 612, 8) == 1);
    CHECK(sc_capture_get_lock(c, &lock) == 1 && lock.sender[0] == 8);
    sc_capture_reset(c);
    /* A fifth key evicts the least recently updated slot deterministically. */
    for (key = 0; key < 5; ++key)
        CHECK(feed(c, h[key], n, 615, (uint32_t)key) == 0);
    for (step = 1; step < 8; ++step)
        CHECK(feed(c, h[0], n, (uint16_t)(615U - step % 4U), 5) == 0);
    sc_capture_reset(c);
    for (key = 0; key < 5; ++key)
        CHECK(feed(c, h[key], n, 615, (uint32_t)key) == 0);
    for (step = 1; step < 8; ++step)
        CHECK(feed(c, h[1], n, (uint16_t)(615U - step % 4U), 5) == (step == 7 ? 1 : 0));
    sc_capture_destroy(c);
}
static void test_result_conflict_ack(void)
{
    sc_capture *c = sc_capture_create();
    uint8_t h[26], bytes[17] = {11, 1, 0, 0, 0, 192, 168, 1, 3, 0xff, 0};
    uint8_t ack[11], before[11];
    sc_touch_result result;
    size_t n = header_make(h, 1, 1, 0), i;
    CHECK(c != NULL);
    memcpy(bytes + 11, bssid, 6);
    bytes[2] = sc_touch_crc8(bytes + 10, 1);
    bytes[3] = sc_touch_crc8(bytes + 11, 6);
    for (i = 0; i < 11; ++i) if (i != 4) bytes[4] ^= bytes[i];
    lock_context(c, h, n, 50, 0);
    CHECK(record(c, h, n, 127, 0, 50, 1) == 1);
    CHECK(record(c, h, n, 127, 1, 50, 1) == 0);
    lock_context(c, h, n, 50, 10);
    for (i = 17; i > 0; --i)
        CHECK(record(c, h, n, (uint8_t)(i - 1), bytes[i - 1], 50, 11) == (i == 1 ? 2 : 1));
    CHECK(sc_capture_get_result(c, &result) == 1);
    CHECK(result.password_len == 1 && result.password[0] == 255);
    CHECK(result.ssid_len == 1 && result.ssid[0] == 0);
    CHECK(sc_capture_tick(c, 999999) == 2);
    CHECK(sc_touch_make_ack(&result, sender, bytes + 5, ack) == 1);
    CHECK(ack[0] == 11 && memcmp(ack + 1, sender, 6) == 0 && memcmp(ack + 7, bytes + 5, 4) == 0);
    memcpy(before, ack, 11);
    result.ssid_len = 33;
    CHECK(sc_touch_make_ack(&result, sender, bytes + 5, ack) == 0);
    CHECK(memcmp(before, ack, 11) == 0);
    sc_capture_reset(c);
    /* An otherwise valid result with a different BSSID must not escape. */
    h[4] = 8;
    lock_context(c, h, n, 50, 20);
    for (i = 17; i > 0; --i)
        CHECK(record(c, h, n, (uint8_t)(i - 1), bytes[i - 1], 50, 21) == (i == 1 ? 0 : 1));
    CHECK(sc_capture_get_result(c, &result) == 0);
    sc_capture_destroy(c);
    CHECK(sc_capture_feed(NULL, h, n, 50, 6, 0) == -1);
    CHECK(sc_capture_tick(NULL, 0) == -1 && sc_capture_get_state(NULL) == -1);
    CHECK(sc_capture_get_result(NULL, &result) == 0);
    CHECK(sc_capture_get_lock(NULL, NULL) == 0);
    CHECK(sc_touch_make_ack(NULL, sender, bytes + 5, ack) == 0);
    sc_capture_reset(NULL); sc_capture_destroy(NULL);
}
int main(void)
{
    test_acquisition();
    test_invalid_and_other_keys();
    test_expiry_and_duplicates();
    test_candidate_capacity();
    test_result_conflict_ack();
    puts("PASS: capture guides, phases, framing classes, bounds, interference, retransmissions, wraparound, deadlines, result binding and ack");
    return 0;
}
