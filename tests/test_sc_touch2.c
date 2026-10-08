#include "sc_touch2.h"
#include "sc_touch2_capture.h"
#include "sc_touch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
typedef struct {
    uint8_t header[6], group[40][6], width[40], cipher[144], plain[144], iv[20], key[16];
    size_t groups, cipher_length;
    sc_touch2_result result;
} fixture;
typedef struct { const fixture *f; unsigned int calls; int fail, bad_padding; } decrypt_spy;
static const uint8_t bssid[6] = {2, 3, 4, 5, 6, 7};
static void segment_add(fixture *f, const uint8_t *bytes, size_t length, size_t width)
{
    size_t position = 0;
    while (position < length) {
        size_t i, count = length - position;
        uint8_t *group;
        if (count > width) count = width;
        CHECK(f->groups < 40); group = f->group[f->groups];
        f->width[f->groups++] = (uint8_t)width;
        for (i = 0; i < width; ++i) group[i] = (uint8_t)(i < count ? bytes[position + i] : 0x55);
        if (width == 5) group[5] = sc_touch_crc8(group, 5);
        position += count;
    }
}
static void make_fixture(fixture *f, size_t password, size_t reserved, size_t ssid,
                          unsigned int flags, unsigned int security)
{
    uint8_t combined[128];
    size_t i;
    memset(f, 0, sizeof(*f));
    f->result.password_len = (uint8_t)password; f->result.reserved_len = (uint8_t)reserved;
    f->result.ssid_len = (uint8_t)ssid; f->result.security_version = (uint8_t)security;
    f->result.port_mark = (uint8_t)(flags % 4U); f->result.ipv4 = (uint8_t)(flags % 2U);
    f->result.bssid_crc = sc_touch_crc8(bssid, 6);
    for (i = 0; i < password; ++i) f->result.password[i] = (uint8_t)((i * 53U) & ((flags & 2U) || security ? 255U : 127U));
    for (i = 0; i < reserved; ++i) f->result.reserved[i] = (uint8_t)((i * 71U) & ((flags & 4U) || security ? 255U : 127U));
    for (i = 0; i < ssid; ++i) f->result.ssid[i] = (uint8_t)((i * 97U) & ((flags & 1U) ? 255U : 127U));
    memcpy(combined, f->result.password, password); memcpy(combined + password, f->result.reserved, reserved);
    f->header[0] = (uint8_t)(ssid | ((flags & 1U) ? 128U : 0U));
    f->header[1] = (uint8_t)(password | ((flags & 2U) ? 128U : 0U));
    f->header[2] = (uint8_t)(reserved | ((flags & 4U) ? 128U : 0U));
    f->header[3] = f->result.bssid_crc;
    f->header[4] = (uint8_t)(f->result.ipv4 | (security << 1) | ((unsigned int)f->result.port_mark << 3));
    f->header[5] = sc_touch_crc8(f->header, 5);
    if (security != 0) {
        size_t meaningful = password + reserved;
        f->cipher_length = 16U * (meaningful / 16U + 1U);
        for (i = 0; i < 16; ++i) f->key[i] = (uint8_t)(0x70U + i);
        for (i = 0; i < 20; ++i) f->iv[i] = (uint8_t)(security == 2 ? 0x90U + i : 0U);
        for (i = 0; i < f->cipher_length; ++i) {
            f->cipher[i] = (uint8_t)(0x40U + i * 37U);
            f->plain[i] = i < meaningful ? combined[i] : (uint8_t)(f->cipher_length - meaningful);
        }
        segment_add(f, f->cipher, f->cipher_length, 5);
        if (security == 2) segment_add(f, f->iv, 20, 5);
    } else if ((flags & 6U) == 0U) segment_add(f, combined, password + reserved, 6);
    else {
        segment_add(f, f->result.password, password, (flags & 2U) ? 5 : 6);
        segment_add(f, f->result.reserved, reserved, (flags & 4U) ? 5 : 6);
    }
    segment_add(f, f->result.ssid, ssid, (flags & 1U) ? 5 : 6);
}
static void planes(const uint8_t bytes[6], size_t width, uint16_t lengths[8])
{
    size_t i, k;
    for (i = 0; i < 8; ++i) {
        unsigned int data = 0;
        for (k = 0; k < 6; ++k) data |= ((bytes[k] >> i) & 1U) << (5U - k);
        if (width == 6 && i == 7) data = sc_touch_crc8(bytes, 6) & 63U;
        lengths[i] = (uint16_t)(64U | (i << 7) | data);
    }
}
static int feed_planes(sc_touch2 *c, const uint8_t bytes[6], size_t width)
{
    uint16_t lengths[8];
    size_t i;
    int state = 0;
    planes(bytes, width, lengths);
    for (i = 8; i > 0; --i) {
        state = sc_touch2_feed(c, lengths[i - 1]);
        CHECK(sc_touch2_feed(c, lengths[i - 1]) == state);
    }
    return state;
}
static int header(sc_touch2 *c, const fixture *f)
{
    (void)sc_touch2_feed(c, 1048); (void)sc_touch2_feed(c, (uint16_t)(1072U + f->groups));
    (void)sc_touch2_feed(c, 1048); (void)sc_touch2_feed(c, (uint16_t)(1072U + f->groups));
    return feed_planes(c, f->header, 5);
}
static int group(sc_touch2 *c, const fixture *f, size_t index)
{
    unsigned int i;
    for (i = 0; i < 3; ++i) (void)sc_touch2_feed(c, (uint16_t)(128U + index));
    return feed_planes(c, f->group[index], f->width[index]);
}
static void unavailable(sc_touch2 *c)
{
    sc_touch2_result r;
    unsigned char before[sizeof(r)];
    memset(&r, 0xa5, sizeof(r)); memcpy(before, &r, sizeof(r));
    CHECK(sc_touch2_get_result(c, &r) == 0 && memcmp(before, &r, sizeof(r)) == 0);
}
static void check_result(sc_touch2 *c, const fixture *f)
{
    sc_touch2_result r;
    uint8_t ack[7]; uint16_t port;
    CHECK(sc_touch2_get_result(c, &r) == 1);
    CHECK(memcmp(&r, &f->result, sizeof(r)) == 0);
    CHECK(sc_touch2_make_ack(&r, bssid, ack, &port) == 1);
    CHECK(ack[0] == r.port_mark && memcmp(ack + 1, bssid, 6) == 0);
    CHECK(port == 18266U + 10000U * r.port_mark);
}
static int spy_decrypt(void *user, const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t *cipher, size_t length, uint8_t *plain)
{
    decrypt_spy *spy = user;
    const fixture *f = spy->f;
    ++spy->calls;
    CHECK(length == f->cipher_length && memcmp(key, f->key, 16) == 0);
    CHECK(memcmp(iv, f->iv, 16) == 0 && memcmp(cipher, f->cipher, length) == 0);
    CHECK(cipher != plain);
    if (spy->fail) return 0;
    memcpy(plain, f->plain, length);
    if (spy->bad_padding) plain[f->result.password_len + f->result.reserved_len] ^= 1U;
    return 1;
}
static void test_plain_layouts(void)
{
    static const size_t sizes[] = {0, 1, 5, 6, 15, 16, 31, 32, 63, 64};
    size_t p, r, s, j;
    unsigned int flags;
    for (p = 0; p < 10; ++p) for (r = 0; r < 10; ++r)
    for (s = 0; s < 8; ++s) for (flags = 0; flags < 8; ++flags) {
        fixture f; sc_touch2 *c = sc_touch2_create(NULL);
        CHECK(c != NULL); make_fixture(&f, sizes[p], sizes[r], sizes[s], flags, 0);
        CHECK(header(c, &f) == (f.groups == 0 ? 1 : 0));
        for (j = f.groups; j > 0; --j) {
            CHECK(group(c, &f, j - 1) == (j == 1 ? 1 : 0));
            CHECK(group(c, &f, j - 1) == (j == 1 ? 1 : 0));
            if (j > 1) { unavailable(c); CHECK(header(c, &f) == 0); }
        }
        check_result(c, &f); CHECK(sc_touch2_feed(c, UINT16_MAX) == 1);
        sc_touch2_destroy(c);
    }
}
static void test_crypto_pipeline(void)
{
    static const size_t sizes[] = {0, 1, 15, 16, 17, 63, 64};
    size_t p, r, j;
    unsigned int security, mode;
    for (p = 0; p < 7; ++p) for (r = 0; r < 7; ++r)
    for (security = 1; security <= 2; ++security) for (mode = 0; mode < 4; ++mode) {
        fixture f; sc_touch2_config config; decrypt_spy spy;
        sc_touch2 *c;
        if (sizes[p] + sizes[r] == 0) continue;
        make_fixture(&f, sizes[p], sizes[r], 32, 7, security);
        memset(&config, 0, sizeof(config)); memcpy(config.key, f.key, 16);
        spy.f = &f; spy.calls = 0; spy.fail = mode == 2; spy.bad_padding = mode == 3;
        config.decrypt = spy_decrypt; config.user = &spy;
        c = sc_touch2_create(mode == 1 ? NULL : &config); CHECK(c != NULL);
        memset(config.key, 0, 16); /* Context must own a copy. */
        CHECK(header(c, &f) == 0);
        for (j = f.groups; j > 0; --j) CHECK(group(c, &f, j - 1) == (j == 1 && mode == 0 ? 1 : 0));
        if (mode == 0) check_result(c, &f); else unavailable(c);
        CHECK(spy.calls == (mode == 1 ? 0U : 1U));
        if (mode == 0) {
            sc_touch2_reset(c); CHECK(header(c, &f) == 0);
            for (j = 0; j < f.groups; ++j) CHECK(group(c, &f, j) == (j + 1 == f.groups ? 1 : 0));
            check_result(c, &f); CHECK(spy.calls == 2);
        }
        sc_touch2_destroy(c);
    }
}
static void test_recovery_conflicts(void)
{
    fixture f, changed;
    sc_touch2 *c = sc_touch2_create(NULL);
    uint16_t lengths[8]; size_t i;
    CHECK(c != NULL); make_fixture(&f, 8, 8, 8, 0, 0);
    CHECK(group(c, &f, 0) == 0); CHECK(header(c, &f) == 0);
    planes(f.group[0], f.width[0], lengths);
    CHECK(sc_touch2_feed(c, 128) == 0);
    for (i = 0; i < 7; ++i) CHECK(sc_touch2_feed(c, lengths[i]) == 0);
    for (i = 1; i < f.groups; ++i) CHECK(group(c, &f, i) == 0);
    unavailable(c); CHECK(group(c, &f, 0) == 1);
    sc_touch2_reset(c); CHECK(header(c, &f) == 0);
    CHECK(sc_touch2_feed(c, 128) == 0); CHECK(sc_touch2_feed(c, lengths[0]) == 0);
    CHECK(sc_touch2_feed(c, (uint16_t)(lengths[0] ^ 1U)) == 0);
    for (i = 1; i < 8; ++i) CHECK(sc_touch2_feed(c, lengths[i]) == 0);
    for (i = 1; i < f.groups; ++i) CHECK(group(c, &f, i) == 0);
    CHECK(group(c, &f, 0) == 1);
    sc_touch2_reset(c); CHECK(header(c, &f) == 0);
    CHECK(sc_touch2_feed(c, 128) == 0);
    for (i = 0; i < 8; ++i) CHECK(sc_touch2_feed(c, (uint16_t)(lengths[i] ^ (i == 7 ? 1U : 0U))) == 0);
    for (i = 1; i < f.groups; ++i) CHECK(group(c, &f, i) == 0);
    unavailable(c); CHECK(group(c, &f, 0) == 1);
    sc_touch2_reset(c); CHECK(header(c, &f) == 0); CHECK(group(c, &f, 0) == 0);
    changed = f; changed.group[0][0] ^= 1U;
    CHECK(group(c, &changed, 0) == -2); unavailable(c);
    CHECK(sc_touch2_feed(c, 1048) == -2);
    sc_touch2_reset(c); CHECK(header(c, &f) == 0);
    CHECK(sc_touch2_feed(c, (uint16_t)(1073U + f.groups)) == -2);
    sc_touch2_reset(c); CHECK(header(c, &f) == 0);
    changed = f; changed.header[3] ^= 1U; changed.header[5] = sc_touch_crc8(changed.header, 5);
    CHECK(header(c, &changed) == -2);
    sc_touch2_reset(c); changed = f; changed.header[5] ^= 1U;
    CHECK(header(c, &changed) == 0); CHECK(group(c, &f, 0) == 0); CHECK(header(c, &f) == 0);
    for (i = 0; i < f.groups; ++i) CHECK(group(c, &f, i) == (i + 1 == f.groups ? 1 : 0));
    sc_touch2_destroy(c);
}
static uint16_t sequence;
static void frame_make(uint8_t h[26], unsigned int direction, unsigned int qos, unsigned int protected_frame)
{
    memset(h, 0, 26); h[0] = (uint8_t)(8U | (qos ? 128U : 0U));
    h[1] = (uint8_t)(direction | (protected_frame ? 64U : 0U));
    memcpy(h + (direction == 1 ? 4 : 10), bssid, 6);
    h[direction == 1 ? 10 : 16] = 4;
    memset(h + (direction == 1 ? 16 : 4), 255, 6);
}
static int cap_feed(sc_touch2_capture *c, uint8_t h[26], size_t bytes, uint16_t length, uint16_t overhead, uint32_t now)
{
    uint16_t seq = (uint16_t)((sequence++ & 4095U) << 4);
    h[22] = (uint8_t)seq; h[23] = (uint8_t)(seq >> 8);
    return sc_touch2_capture_feed(c, h, bytes, (uint16_t)(length + overhead), 6, now);
}
static void test_capture(void)
{
    unsigned int direction, qos, protected_frame, phase;
    for (direction = 1; direction <= 2; ++direction) for (qos = 0; qos < 2; ++qos)
    for (protected_frame = 0; protected_frame < 2; ++protected_frame) for (phase = 0; phase < 2; ++phase) {
        fixture f; sc_touch2_capture *c = sc_touch2_capture_create(NULL);
        sc_capture_lock lock; sc_touch2_result r; uint8_t h[26]; uint16_t lengths[8];
        size_t i, j, bytes = qos ? 26 : 24;
        uint32_t time = UINT32_MAX - 1000U;
        CHECK(c != NULL); make_fixture(&f, 5, 5, 5, 7, 0);
        frame_make(h, direction, qos, protected_frame);
        for (i = phase; i < 6; ++i)
            if (cap_feed(c, h, bytes, (uint16_t)(i % 2 ? 1072U + f.groups : 1048U), 64, time) == 1) break;
        CHECK(sc_touch2_capture_get_lock(c, &lock) == 1 && lock.overhead == 64);
        planes(f.header, 5, lengths);
        for (i = 0; i < 8; ++i) CHECK(cap_feed(c, h, bytes, lengths[i], 64, time) == 1);
        for (j = f.groups; j > 0; --j) {
            CHECK(cap_feed(c, h, bytes, (uint16_t)(127U + j), 64, time) == 1);
            planes(f.group[j - 1], f.width[j - 1], lengths);
            for (i = 0; i < 8; ++i) CHECK(cap_feed(c, h, bytes, lengths[i], 64, time) == (j == 1 && i == 7 ? 2 : 1));
        }
        CHECK(sc_touch2_capture_get_result(c, &r) == 1 && memcmp(&r, &f.result, sizeof(r)) == 0);
        CHECK(sc_touch2_capture_tick(c, time + 30000U) == 2);
        sc_touch2_capture_reset(c);
        for (i = 0; i < 4; ++i) CHECK(cap_feed(c, h, bytes, (uint16_t)(i % 2 ? 1072U + f.groups : 1048U), 64, time) == (i == 3 ? 1 : 0));
        CHECK(sc_touch2_capture_tick(c, time + 2500U) == 0);
        sc_touch2_capture_destroy(c);
    }
}
static int capture_message(sc_touch2_capture *c, uint8_t h[26], const fixture *f, uint16_t overhead)
{
    uint16_t lengths[8]; size_t i, j;
    int state = 0;
    for (i = 0; i < 4; ++i)
        CHECK(cap_feed(c, h, 26, (uint16_t)(i % 2 ? 1072U + f->groups : 1048U), overhead, 100) == (i == 3 ? 1 : 0));
    planes(f->header, 5, lengths);
    for (i = 0; i < 8; ++i) CHECK(cap_feed(c, h, 26, lengths[i], overhead, 101) == 1);
    for (j = 0; j < f->groups; ++j) {
        CHECK(cap_feed(c, h, 26, (uint16_t)(128U + j), overhead, 102) == 1);
        planes(f->group[j], f->width[j], lengths);
        for (i = 0; i < 8; ++i) state = cap_feed(c, h, 26, lengths[i], overhead, 102);
    }
    return state;
}
static void test_capture_crypto_binding(void)
{
    unsigned int security;
    for (security = 1; security <= 2; ++security) {
        fixture f; sc_touch2_config config; decrypt_spy spy; sc_touch2_result result;
        sc_touch2_capture *c; uint8_t h[26];
        make_fixture(&f, 64, 64, 32, 7, security);
        memset(&config, 0, sizeof(config)); memcpy(config.key, f.key, 16);
        spy.f = &f; spy.calls = 0; spy.fail = 0; spy.bad_padding = 0;
        config.decrypt = spy_decrypt; config.user = &spy;
        c = sc_touch2_capture_create(&config); CHECK(c != NULL);
        frame_make(h, 1, 1, 1);
        CHECK(capture_message(c, h, &f, 0) == 2);
        CHECK(sc_touch2_capture_get_result(c, &result) == 1 && memcmp(&result, &f.result, sizeof(result)) == 0);
        sc_touch2_capture_reset(c);
        CHECK(capture_message(c, h, &f, 256) == 2 && spy.calls == 2);
        sc_touch2_capture_reset(c); h[4] = 8; /* Lock a different, still unicast BSSID. */
        CHECK(capture_message(c, h, &f, 64) == 0);
        CHECK(sc_touch2_capture_get_result(c, &result) == 0);
        sc_touch2_capture_destroy(c);
        c = sc_touch2_capture_create(NULL); CHECK(c != NULL); frame_make(h, 1, 1, 1);
        CHECK(capture_message(c, h, &f, 64) == 1);
        CHECK(sc_touch2_capture_get_result(c, &result) == 0);
        sc_touch2_capture_destroy(c);
    }
}
static void test_bounds_ack(void)
{
    fixture f, bad; sc_touch2 *c = sc_touch2_create(NULL);
    unsigned int variant; size_t i;
    uint8_t ack[7], old[7]; uint16_t port = 123;
    make_fixture(&f, 5, 5, 5, 7, 0); CHECK(c != NULL);
    for (variant = 0; variant < 7; ++variant) {
        sc_touch2_reset(c); bad = f;
        switch (variant) {
        case 0: bad.header[0] = 33; break;
        case 1: bad.header[1] = 65; break;
        case 2: bad.header[2] = 65; break;
        case 3: bad.header[4] |= 32U; break;
        case 4: bad.header[4] |= 64U; break;
        case 5: bad.header[4] |= 6U; break;
        case 6: ++bad.groups; break;
        default: abort();
        }
        bad.header[5] = sc_touch_crc8(bad.header, 5);
        CHECK(header(c, &bad) == 0); unavailable(c); CHECK(header(c, &f) == 0);
        for (i = 0; i < f.groups; ++i) CHECK(group(c, &f, i) == (i + 1 == f.groups ? 1 : 0));
    }
    memset(ack, 0xa5, sizeof(ack)); memcpy(old, ack, sizeof(ack));
    f.result.port_mark = 4;
    CHECK(sc_touch2_make_ack(&f.result, bssid, ack, &port) == 0 && port == 123 && memcmp(old, ack, 7) == 0);
    CHECK(sc_touch2_feed(NULL, 1) == -1); unavailable(NULL);
    CHECK(sc_touch2_make_ack(NULL, bssid, ack, &port) == 0);
    sc_touch2_reset(NULL); sc_touch2_destroy(NULL); sc_touch2_destroy(c);
}
int main(void)
{
    test_plain_layouts(); test_crypto_pipeline(); test_recovery_conflicts(); test_capture(); test_capture_crypto_binding(); test_bounds_ack();
    puts("PASS: v2 plaintext layouts/planes, callback plumbing/padding (not AES validation), CRC/conflict/recovery, capture and ack bounds");
    return 0;
}
