#include "sc_touch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* Independent expression of the specification's bit recurrence. */
static uint8_t reference_crc(const uint8_t *bytes, size_t count)
{
    unsigned int state = 0;
    size_t i;
    unsigned int bit;

    for (i = 0; i < count; ++i) {
        unsigned int input = bytes[i];
        for (bit = 0; bit < 8; ++bit) {
            unsigned int feedback = (state ^ input) & 1U;
            state /= 2U;
            input /= 2U;
            state ^= feedback * 0x8cU;
        }
    }
    return (uint8_t)state;
}

static void encode(uint8_t index, uint8_t value, uint16_t lengths[3])
{
    uint8_t pair[2] = {value, index};
    unsigned int checksum = reference_crc(pair, sizeof(pair));

    lengths[0] = (uint16_t)(40U + (checksum & 240U) + (value >> 4));
    lengths[1] = (uint16_t)(296U + index);
    lengths[2] = (uint16_t)(40U + ((checksum & 15U) << 4) + (value & 15U));
}

static int feed_record(sc_touch *ctx, uint8_t index, uint8_t value)
{
    uint16_t lengths[3];
    encode(index, value, lengths);
    (void)sc_touch_feed(ctx, lengths[0]);
    (void)sc_touch_feed(ctx, lengths[1]);
    return sc_touch_feed(ctx, lengths[2]);
}

static void update_xor(uint8_t bytes[128])
{
    size_t i;
    uint8_t checksum = 0;
    for (i = 0; i < bytes[0]; ++i) {
        if (i != 4) {
            checksum = (uint8_t)(checksum ^ bytes[i]);
        }
    }
    bytes[4] = checksum;
}

static size_t make_message(size_t password_length, size_t ssid_length,
                           uint8_t bytes[128], sc_touch_result *expected)
{
    size_t i;
    size_t total = 9 + password_length + ssid_length;
    memset(bytes, 0, 128);
    memset(expected, 0, sizeof(*expected));
    bytes[0] = (uint8_t)total;
    bytes[1] = (uint8_t)password_length;
    expected->password_len = (uint8_t)password_length;
    expected->ssid_len = (uint8_t)ssid_length;
    for (i = 0; i < password_length; ++i) {
        bytes[9 + i] = (uint8_t)(i * 53U);
        expected->password[i] = bytes[9 + i];
    }
    for (i = 0; i < ssid_length; ++i) {
        bytes[9 + password_length + i] = (uint8_t)(i * 77U);
        expected->ssid[i] = bytes[9 + password_length + i];
    }
    for (i = 0; i < 6; ++i) {
        bytes[total + i] = (uint8_t)(0x80U + i);
        expected->bssid[i] = bytes[total + i];
    }
    for (i = 0; i < 4; ++i) {
        bytes[5 + i] = (uint8_t)(192U + i);
        expected->sender_ip[i] = bytes[5 + i];
    }
    bytes[2] = reference_crc(bytes + 9 + password_length, ssid_length);
    bytes[3] = reference_crc(bytes + total, 6);
    update_xor(bytes);
    return total + 6;
}

static void check_result(const sc_touch_result *actual,
                         const sc_touch_result *expected)
{
    CHECK(actual->ssid_len == expected->ssid_len);
    CHECK(actual->password_len == expected->password_len);
    CHECK(memcmp(actual->ssid, expected->ssid, sizeof(actual->ssid)) == 0);
    CHECK(memcmp(actual->password, expected->password, sizeof(actual->password)) == 0);
    CHECK(memcmp(actual->bssid, expected->bssid, sizeof(actual->bssid)) == 0);
    CHECK(memcmp(actual->sender_ip, expected->sender_ip, sizeof(actual->sender_ip)) == 0);
}

static void check_unavailable(const sc_touch *ctx)
{
    sc_touch_result out;
    unsigned char before[sizeof(out)];
    memset(&out, 0xa5, sizeof(out));
    memcpy(before, &out, sizeof(out));
    CHECK(sc_touch_get_result(ctx, &out) == 0);
    CHECK(memcmp(before, &out, sizeof(out)) == 0);
}

static void test_null_arguments(void)
{
    uint16_t lengths[3];
    uint8_t index = 37;
    uint8_t value = 41;
    sc_touch *ctx = sc_touch_create();
    CHECK(ctx != NULL);
    encode(1, 2, lengths);
    CHECK(sc_touch_crc8(NULL, 0) == 0);
    CHECK(sc_touch_crc8(NULL, 8) == 0);
    CHECK(sc_touch_decode_triplet(NULL, &index, &value) == 0);
    CHECK(sc_touch_decode_triplet(lengths, NULL, &value) == 0);
    CHECK(sc_touch_decode_triplet(lengths, &index, NULL) == 0);
    CHECK(index == 37 && value == 41);
    CHECK(sc_touch_feed(NULL, 100) == -1);
    CHECK(sc_touch_get_result(ctx, NULL) == 0);
    CHECK(sc_touch_get_result(NULL, NULL) == 0);
    check_unavailable(NULL);
    check_unavailable(ctx);
    sc_touch_reset(NULL);
    sc_touch_destroy(NULL);
    sc_touch_destroy(ctx);
}

static void test_triplets(void)
{
    unsigned int idx;
    unsigned int val;
    size_t pos;
    uint16_t lengths[3];
    uint8_t index;
    uint8_t value;

    for (idx = 0; idx < 128; ++idx) {
        for (val = 0; val < 256; ++val) {
            uint8_t pair[2] = {(uint8_t)val, (uint8_t)idx};
            CHECK(sc_touch_crc8(pair, 2) == reference_crc(pair, 2));
            encode((uint8_t)idx, (uint8_t)val, lengths);
            CHECK(sc_touch_decode_triplet(lengths, &index, &value) == 1);
            CHECK(index == idx && value == val);
            /* Toggle a transmitted checksum bit while preserving range/data. */
            lengths[0] = (uint16_t)(40U + ((lengths[0] - 40U) ^ 16U));
            index = 251;
            value = 252;
            CHECK(sc_touch_decode_triplet(lengths, &index, &value) == 0);
            CHECK(index == 251 && value == 252);
        }
    }
    for (pos = 0; pos < 3; ++pos) {
        for (val = 0; val <= UINT16_MAX; ++val) {
            unsigned int min = pos == 1 ? 296U : 40U;
            unsigned int max = pos == 1 ? 423U : 295U;
            if (val < min || val > max) {
                encode(10, 200, lengths);
                lengths[pos] = (uint16_t)val;
                index = 251;
                value = 252;
                CHECK(sc_touch_decode_triplet(lengths, &index, &value) == 0);
                CHECK(index == 251 && value == 252);
            }
        }
    }
}

static void test_all_lengths_and_orders(void)
{
    size_t password_length;
    size_t ssid_length;
    uint32_t random_state = UINT32_C(193);

    for (password_length = 0; password_length <= 64; ++password_length) {
        for (ssid_length = 0; ssid_length <= 32; ++ssid_length) {
            uint8_t bytes[128];
            uint8_t order[128];
            sc_touch_result expected;
            sc_touch_result actual;
            sc_touch *ctx = sc_touch_create();
            size_t count = make_message(password_length, ssid_length, bytes, &expected);
            size_t i;
            CHECK(ctx != NULL);
            for (i = 0; i < count; ++i) {
                order[i] = (uint8_t)i;
            }
            for (i = count; i > 1; --i) {
                size_t j;
                uint8_t temp;
                random_state = random_state * UINT32_C(1664525) + UINT32_C(1013904223);
                j = (size_t)random_state % i;
                temp = order[i - 1];
                order[i - 1] = order[j];
                order[j] = temp;
            }
            for (i = 0; i < count; ++i) {
                int wanted = i + 1 == count ? 1 : 0;
                CHECK(feed_record(ctx, order[i], bytes[order[i]]) == wanted);
                CHECK(feed_record(ctx, order[i], bytes[order[i]]) == wanted);
                CHECK(sc_touch_feed(ctx, 515) == wanted);
                if (wanted == 0) {
                    check_unavailable(ctx);
                }
            }
            CHECK(sc_touch_get_result(ctx, &actual) == 1);
            check_result(&actual, &expected);
            CHECK(feed_record(ctx, 0, 0) == 1);
            CHECK(sc_touch_feed(ctx, UINT16_MAX) == 1);
            CHECK(sc_touch_get_result(ctx, &actual) == 1);
            check_result(&actual, &expected);
            sc_touch_destroy(ctx);
        }
    }
}

static void test_framing_and_contexts(void)
{
    sc_touch *a = sc_touch_create();
    sc_touch *b = sc_touch_create();
    sc_touch_result expected_a;
    sc_touch_result expected_b;
    sc_touch_result actual;
    uint8_t bytes_a[128];
    uint8_t bytes_b[128];
    uint16_t lengths[3];
    size_t count_a = make_message(8, 7, bytes_a, &expected_a);
    size_t count_b = make_message(3, 2, bytes_b, &expected_b);
    size_t i;
    CHECK(a != NULL && b != NULL);
    /* Index 0 is collected. Broken index 1 must not erase it or be accepted. */
    CHECK(feed_record(a, 0, bytes_a[0]) == 0);
    encode(1, bytes_a[1], lengths);
    CHECK(sc_touch_feed(a, lengths[0]) == 0);
    CHECK(sc_touch_feed(a, lengths[1]) == 0);
    CHECK(sc_touch_feed(a, 512) == 0);
    CHECK(sc_touch_feed(a, lengths[2]) == 0);
    for (i = 2; i < count_a; ++i) {
        CHECK(feed_record(a, (uint8_t)i, bytes_a[i]) == 0);
        if (i - 2 < count_b) {
            size_t j = i - 2;
            CHECK(feed_record(b, (uint8_t)j, bytes_b[j]) == (j + 1 == count_b ? 1 : 0));
        }
    }
    check_unavailable(a);
    CHECK(sc_touch_get_result(b, &actual) == 1);
    check_result(&actual, &expected_b);
    /* Malformed checksum followed immediately by an intact triplet slides in. */
    lengths[2] = (uint16_t)(40U + ((lengths[2] - 40U) ^ 16U));
    CHECK(sc_touch_feed(a, lengths[0]) == 0);
    CHECK(sc_touch_feed(a, lengths[1]) == 0);
    CHECK(sc_touch_feed(a, lengths[2]) == 0);
    check_unavailable(a);
    CHECK(feed_record(a, 1, bytes_a[1]) == 1);
    CHECK(sc_touch_get_result(a, &actual) == 1);
    check_result(&actual, &expected_a);
    sc_touch_reset(a);
    check_unavailable(a);
    /* Reset must also clear an unfinished length window. */
    encode(0, bytes_b[0], lengths);
    CHECK(sc_touch_feed(a, lengths[0]) == 0);
    CHECK(sc_touch_feed(a, lengths[1]) == 0);
    sc_touch_reset(a);
    CHECK(sc_touch_feed(a, lengths[2]) == 0);
    for (i = 1; i < count_b; ++i) {
        CHECK(feed_record(a, (uint8_t)i, bytes_b[i]) == 0);
    }
    CHECK(feed_record(a, 0, bytes_b[0]) == 1);
    CHECK(sc_touch_get_result(a, &actual) == 1);
    check_result(&actual, &expected_b);
    sc_touch_destroy(a);
    sc_touch_destroy(b);
}

static void test_conflicts(void)
{
    sc_touch *ctx = sc_touch_create();
    unsigned int index;
    CHECK(ctx != NULL);
    for (index = 0; index < 128; ++index) {
        sc_touch_reset(ctx);
        CHECK(feed_record(ctx, (uint8_t)index, 200) == 0);
        CHECK(feed_record(ctx, (uint8_t)index, 200) == 0);
        CHECK(feed_record(ctx, (uint8_t)index, 201) == -2);
        CHECK(sc_touch_feed(ctx, 0) == -2);
        CHECK(feed_record(ctx, (uint8_t)index, 200) == -2);
        check_unavailable(ctx);
    }
    sc_touch_reset(ctx);
    CHECK(sc_touch_feed(ctx, 0) == 0);
    sc_touch_destroy(ctx);
}

static void test_invalid_messages(void)
{
    unsigned int variant;
    for (variant = 0; variant < 8; ++variant) {
        uint8_t bytes[128];
        sc_touch_result expected;
        sc_touch *ctx = sc_touch_create();
        size_t i;
        CHECK(ctx != NULL);
        (void)make_message(8, 7, bytes, &expected);
        switch (variant) {
        case 0: bytes[0] = 8; break;
        case 1: bytes[0] = 106; break;
        case 2: bytes[1] = 65; break;
        case 3: bytes[1] = 16; break; /* More password than total permits. */
        case 4: bytes[0] = 50; break; /* SSID length 33. */
        case 5: bytes[2] ^= 1U; update_xor(bytes); break;
        case 6: bytes[3] ^= 1U; update_xor(bytes); break;
        case 7: bytes[4] ^= 1U; break;
        default: abort();
        }
        for (i = 0; i < 128; ++i) {
            CHECK(feed_record(ctx, (uint8_t)i, bytes[i]) == 0);
        }
        check_unavailable(ctx);
        sc_touch_destroy(ctx);
    }
}

int main(void)
{
    test_null_arguments();
    test_triplets();
    test_all_lengths_and_orders();
    test_framing_and_contexts();
    test_conflicts();
    test_invalid_messages();
    puts("PASS: exhaustive triplets, all credential lengths, framing, integrity, conflicts, reset, independent contexts");
    return EXIT_SUCCESS;
}
