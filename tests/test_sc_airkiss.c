#include "sc_airkiss.h"
#include "sc_airkiss_capture.h"
#include "sc_touch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
typedef struct {
    uint8_t data[97];
    uint16_t magic[4], prefix[4];
    size_t total;
    sc_airkiss_result expected;
} fixture;
static void make_fixture(fixture *f, size_t password, size_t ssid)
{
    size_t i;
    uint8_t p = (uint8_t)password;
    unsigned int crc;
    memset(f, 0, sizeof(*f));
    f->total = password + ssid + 1;
    f->expected.password_len = p;
    f->expected.ssid_len = (uint8_t)ssid;
    f->expected.token = 0x93;
    for (i = 0; i < password; ++i) f->data[i] = f->expected.password[i] = (uint8_t)(i * 51U);
    f->data[password] = f->expected.token;
    for (i = 0; i < ssid; ++i) f->data[password + 1 + i] = f->expected.ssid[i] = (uint8_t)(i * 97U);
    crc = sc_touch_crc8(f->expected.ssid, ssid);
    f->magic[0] = (uint16_t)(f->total < 16 ? 8 : f->total >> 4);
    f->magic[1] = (uint16_t)(16U + (f->total & 15U));
    f->magic[2] = (uint16_t)(32U + (crc >> 4));
    f->magic[3] = (uint16_t)(48U + (crc & 15U));
    crc = sc_touch_crc8(&p, 1);
    f->prefix[0] = (uint16_t)(64U + (password >> 4));
    f->prefix[1] = (uint16_t)(80U + (password & 15U));
    f->prefix[2] = (uint16_t)(96U + (crc >> 4));
    f->prefix[3] = (uint16_t)(112U + (crc & 15U));
}
static size_t block_symbols(const fixture *f, size_t index, uint16_t symbols[6])
{
    uint8_t crc_input[5];
    size_t i, count = f->total - 4 * index;
    if (count > 4) count = 4;
    crc_input[0] = (uint8_t)index;
    memcpy(crc_input + 1, f->data + 4 * index, count);
    symbols[0] = (uint16_t)(128U + (sc_touch_crc8(crc_input, count + 1) & 127U));
    symbols[1] = (uint16_t)(128U + index);
    for (i = 0; i < count; ++i) symbols[2 + i] = (uint16_t)(256U + crc_input[i + 1]);
    return count + 2;
}
static int send_symbols(sc_airkiss *c, const uint16_t *symbols, size_t count)
{
    size_t i;
    int result = 0;
    for (i = 0; i < count; ++i) result = sc_airkiss_feed(c, symbols[i]);
    return result;
}
static void metadata(sc_airkiss *c, const fixture *f)
{
    CHECK(send_symbols(c, f->magic, 4) == 0);
    CHECK(send_symbols(c, f->prefix, 4) == 0);
}
static int block(sc_airkiss *c, const fixture *f, size_t index)
{
    uint16_t symbols[6];
    size_t count = block_symbols(f, index, symbols);
    return send_symbols(c, symbols, count);
}
static void check_result(sc_airkiss *c, const fixture *f)
{
    sc_airkiss_result r;
    uint8_t ack;
    CHECK(sc_airkiss_get_result(c, &r) == 1);
    CHECK(r.ssid_len == f->expected.ssid_len && r.password_len == f->expected.password_len);
    CHECK(memcmp(r.ssid, f->expected.ssid, 32) == 0);
    CHECK(memcmp(r.password, f->expected.password, 64) == 0 && r.token == f->expected.token);
    CHECK(sc_airkiss_make_ack(&r, &ack) == 1 && ack == r.token);
}
static void unavailable(sc_airkiss *c)
{
    sc_airkiss_result r;
    unsigned char old[sizeof(r)];
    memset(&r, 0xac, sizeof(r)); memcpy(old, &r, sizeof(r));
    CHECK(sc_airkiss_get_result(c, &r) == 0 && memcmp(old, &r, sizeof(r)) == 0);
}
static void test_all_lengths(void)
{
    size_t p, s;
    for (p = 0; p <= 64; ++p) for (s = 0; s <= 32; ++s) {
        fixture f;
        sc_airkiss *c = sc_airkiss_create();
        size_t blocks, j;
        CHECK(c != NULL);
        make_fixture(&f, p, s); blocks = (f.total + 3) / 4;
        CHECK(block(c, &f, 0) == 0); /* Payload before metadata is ignored. */
        metadata(c, &f);
        for (j = blocks; j > 0; --j) {
            int expected = j == 1 ? 1 : 0;
            CHECK(block(c, &f, j - 1) == expected);
            CHECK(block(c, &f, j - 1) == expected);
            if (expected == 0) { unavailable(c); metadata(c, &f); }
        }
        check_result(c, &f);
        CHECK(sc_airkiss_feed(c, UINT16_MAX) == 1);
        sc_airkiss_reset(c); unavailable(c);
        sc_airkiss_destroy(c);
    }
}
static void test_recovery(void)
{
    fixture f;
    sc_airkiss *c = sc_airkiss_create();
    uint16_t symbols[6], metadata_bad[4];
    size_t count, drop, i;
    CHECK(c != NULL); make_fixture(&f, 4, 4);
    for (drop = 0; drop < 4; ++drop) {
        sc_airkiss_reset(c);
        for (i = 0; i < 4; ++i) if (i != drop) (void)sc_airkiss_feed(c, f.magic[i]);
        metadata(c, &f);
        count = block_symbols(&f, 0, symbols);
        for (i = 0; i < count; ++i) if (i != drop) (void)sc_airkiss_feed(c, symbols[i]);
        CHECK(block(c, &f, 1) == 0); CHECK(block(c, &f, 2) == 0);
        CHECK(block(c, &f, 0) == 1); check_result(c, &f);
    }
    sc_airkiss_reset(c); metadata(c, &f);
    count = block_symbols(&f, 0, symbols); symbols[0] ^= 1U;
    CHECK(send_symbols(c, symbols, count) == 0);
    CHECK(block(c, &f, 1) == 0); CHECK(block(c, &f, 2) == 0);
    unavailable(c); CHECK(block(c, &f, 0) == 1);
    sc_airkiss_reset(c);
    memcpy(metadata_bad, f.magic, sizeof(metadata_bad)); metadata_bad[0] = 0;
    CHECK(send_symbols(c, metadata_bad, 4) == 0);
    CHECK(send_symbols(c, f.prefix, 4) == 0);
    CHECK(block(c, &f, 0) == 0);
    metadata(c, &f);
    /* Extra headers, invalid indexes and out-of-range lengths cannot stick. */
    CHECK(sc_airkiss_feed(c, 128) == 0);
    CHECK(sc_airkiss_feed(c, 255) == 0);
    CHECK(sc_airkiss_feed(c, 256) == 0);
    CHECK(sc_airkiss_feed(c, UINT16_MAX) == 0);
    CHECK(sc_airkiss_feed(c, 129) == 0);
    CHECK(block(c, &f, 0) == 0); CHECK(block(c, &f, 2) == 0);
    unavailable(c); CHECK(block(c, &f, 1) == 1);
    sc_airkiss_reset(c);
    CHECK(send_symbols(c, f.magic, 4) == 0);
    memcpy(metadata_bad, f.prefix, sizeof(metadata_bad)); metadata_bad[3] ^= 1U;
    CHECK(send_symbols(c, metadata_bad, 4) == 0);
    CHECK(block(c, &f, 0) == 0);
    CHECK(send_symbols(c, f.prefix, 4) == 0);
    CHECK(block(c, &f, 0) == 0); CHECK(block(c, &f, 1) == 0); CHECK(block(c, &f, 2) == 1);
    sc_airkiss_destroy(c);
}
static void test_conflicts_and_integrity(void)
{
    fixture f, changed;
    sc_airkiss *c = sc_airkiss_create();
    sc_airkiss *other = sc_airkiss_create();
    CHECK(c != NULL && other != NULL);
    make_fixture(&f, 4, 4); metadata(c, &f); metadata(other, &f);
    CHECK(block(c, &f, 0) == 0);
    changed = f; changed.data[0] ^= 1U;
    CHECK(block(c, &changed, 0) == -2);
    CHECK(sc_airkiss_feed(c, 1) == -2); unavailable(c);
    CHECK(block(other, &f, 0) == 0); CHECK(block(other, &f, 1) == 0); CHECK(block(other, &f, 2) == 1);
    check_result(other, &f); sc_airkiss_destroy(other);
    sc_airkiss_reset(c); metadata(c, &f);
    changed = f; changed.magic[3] ^= 1U;
    CHECK(send_symbols(c, changed.magic, 4) == -2);
    sc_airkiss_reset(c); metadata(c, &f);
    make_fixture(&changed, 5, 3);
    CHECK(send_symbols(c, changed.prefix, 4) == -2);
    sc_airkiss_reset(c); changed = f; changed.magic[3] ^= 1U;
    metadata(c, &changed);
    CHECK(block(c, &changed, 0) == 0); CHECK(block(c, &changed, 1) == 0); CHECK(block(c, &changed, 2) == 0);
    unavailable(c);
    sc_airkiss_destroy(c);
}
static void test_invalid_metadata_bounds(void)
{
    fixture f;
    sc_airkiss *c = sc_airkiss_create();
    unsigned int variant;
    size_t i, blocks;
    uint16_t bad[4];
    CHECK(c != NULL); make_fixture(&f, 8, 31); blocks = (f.total + 3) / 4;
    for (variant = 0; variant < 6; ++variant) {
        sc_airkiss_reset(c);
        if (variant < 3) {
            memcpy(bad, f.magic, sizeof(bad));
            if (variant == 0) { bad[0] = 8; bad[1] = 16; } /* T=0 */
            if (variant == 1) { bad[0] = 6; bad[1] = 18; } /* T=98 */
            if (variant == 2) { bad[0] = 7; bad[1] = 16; } /* bad high nibble */
            CHECK(send_symbols(c, bad, 4) == 0);
            metadata(c, &f);
        } else {
            uint8_t p = (uint8_t)(variant == 3 ? 65 : variant == 4 ? 40 : 0);
            unsigned int crc = sc_touch_crc8(&p, 1);
            CHECK(send_symbols(c, f.magic, 4) == 0);
            bad[0] = (uint16_t)(64U + (p >> 4)); bad[1] = (uint16_t)(80U + (p & 15U));
            bad[2] = (uint16_t)(96U + (crc >> 4)); bad[3] = (uint16_t)(112U + (crc & 15U));
            CHECK(send_symbols(c, bad, 4) == 0);
            CHECK(block(c, &f, 0) == 0);
            CHECK(send_symbols(c, f.prefix, 4) == 0);
        }
        for (i = 0; i < blocks; ++i) CHECK(block(c, &f, i) == (i + 1 == blocks ? 1 : 0));
        check_result(c, &f);
    }
    sc_airkiss_destroy(c);
}
static uint16_t sequence;
static int capture_symbol(sc_airkiss_capture *c, uint8_t header[26], size_t bytes, uint16_t symbol, uint32_t time)
{
    uint16_t seq = (uint16_t)((sequence++ & 4095U) << 4);
    header[22] = (uint8_t)seq; header[23] = (uint8_t)(seq >> 8);
    return sc_airkiss_capture_feed(c, header, bytes, (uint16_t)(symbol + 64U), 6, time);
}
static void capture_header(uint8_t h[26], unsigned int direction, unsigned int qos, unsigned int protected_frame)
{
    uint8_t bssid[6] = {2, 3, 4, 5, 6, 7}, sender[6] = {4, 3, 4, 5, 6, 8};
    memset(h, 0, 26); h[0] = (uint8_t)(8U + qos * 128U); h[1] = (uint8_t)(direction + protected_frame * 64U);
    memcpy(h + (direction == 1 ? 4 : 10), bssid, 6);
    memcpy(h + (direction == 1 ? 10 : 16), sender, 6);
    memset(h + (direction == 1 ? 16 : 4), 255, 6);
}
static void test_capture(void)
{
    unsigned int direction, qos, protected_frame, phase;
    for (direction = 1; direction <= 2; ++direction) for (qos = 0; qos < 2; ++qos)
    for (protected_frame = 0; protected_frame < 2; ++protected_frame) for (phase = 0; phase < 4; ++phase) {
        sc_airkiss_capture *c = sc_airkiss_capture_create();
        sc_capture_lock lock;
        sc_airkiss_result result;
        fixture f;
        uint8_t h[26], other[26];
        size_t bytes = qos ? 26 : 24, j, k, count;
        unsigned int i;
        uint16_t symbols[6];
        uint32_t time = UINT32_MAX - 1000U;
        CHECK(c != NULL); capture_header(h, direction, qos, protected_frame);
        for (i = phase; i < phase + 4; ++i) {
            int state = capture_symbol(c, h, bytes, (uint16_t)(1U + i % 4U), time);
            CHECK(state == (i - phase == 3 ? 1 : 0));
        }
        CHECK(sc_airkiss_capture_get_lock(c, &lock) == 1 && lock.overhead == 64 && lock.channel == 6);
        CHECK(sc_airkiss_capture_tick(c, time + 2499U) == 1);
        CHECK(sc_airkiss_capture_feed(c, h, bytes, 68, 6, time + 2499U) == 1); /* duplicate */
        CHECK(sc_airkiss_capture_tick(c, time + 2500U) == 0);
        for (i = 0; i < 4; ++i) CHECK(capture_symbol(c, h, bytes, (uint16_t)(1U + i % 4U), 2000) == (i == 3 ? 1 : 0));
        make_fixture(&f, 5, 3);
        memcpy(other, h, 26); other[direction == 1 ? 10 : 16] = 6;
        for (j = 0; j < 4; ++j) {
            CHECK(capture_symbol(c, other, bytes, f.magic[j], 2001) == 1);
            CHECK(capture_symbol(c, h, bytes, f.magic[j], 2001) == 1);
        }
        for (j = 0; j < 4; ++j) CHECK(capture_symbol(c, h, bytes, f.prefix[j], 2001) == 1);
        for (j = (f.total + 3) / 4; j > 0; --j) {
            count = block_symbols(&f, j - 1, symbols);
            for (k = 0; k < count; ++k)
                CHECK(capture_symbol(c, h, bytes, symbols[k], 2001) == (j == 1 && k + 1 == count ? 2 : 1));
        }
        CHECK(sc_airkiss_capture_get_result(c, &result) == 1 && result.token == f.expected.token);
        CHECK(sc_airkiss_capture_tick(c, 999999) == 2);
        sc_airkiss_capture_reset(c);
        for (i = 0; i < 4; ++i) CHECK(capture_symbol(c, h, bytes, (uint16_t)(1U + i % 4U), 0) == (i == 3 ? 1 : 0));
        for (i = 1; i < 30; ++i) CHECK(capture_symbol(c, h, bytes, 1, i * 1000U) == 1);
        CHECK(sc_airkiss_capture_tick(c, 30000) == 0);
        for (i = 0; i < 4; ++i) CHECK(capture_symbol(c, h, bytes, (uint16_t)(1U + i % 4U), 31000) == (i == 3 ? 1 : 0));
        for (j = 0; j < 4; ++j) CHECK(capture_symbol(c, h, bytes, f.magic[j], 31001) == 1);
        f.magic[3] ^= 1U;
        for (j = 0; j < 4; ++j) CHECK(capture_symbol(c, h, bytes, f.magic[j], 31001) == (j == 3 ? 0 : 1));
        sc_airkiss_capture_destroy(c);
    }
}
static void test_guide_permutations(void)
{
    unsigned int a,b,c,d;
    for(a=0;a<4;++a)for(b=0;b<4;++b)for(c=0;c<4;++c)for(d=0;d<4;++d){
        unsigned int values[4]={a,b,c,d},i;int distinct=a!=b&&a!=c&&a!=d&&b!=c&&b!=d&&c!=d;
        sc_airkiss_capture *ctx=sc_airkiss_capture_create();uint8_t h[26];capture_header(h,1,0,0);
        for(i=0;i<4;++i)CHECK(capture_symbol(ctx,h,24,(uint16_t)(1U+values[i]),i)==(i==3&&distinct?1:0));
        if(!distinct){int state=0;for(i=0;i<4;++i)state=capture_symbol(ctx,h,24,(uint16_t)(1U+i),10U+i);CHECK(state==1);}
        sc_airkiss_capture_destroy(ctx);
    }
}
static void test_null_and_ack(void)
{
    sc_airkiss_result r;
    uint8_t ack = 17;
    memset(&r, 0, sizeof(r)); r.password_len = 65;
    CHECK(sc_airkiss_make_ack(&r, &ack) == 0 && ack == 17);
    r.password_len = 0; r.ssid_len = 33;
    CHECK(sc_airkiss_make_ack(&r, &ack) == 0 && ack == 17);
    CHECK(sc_airkiss_make_ack(NULL, &ack) == 0 && sc_airkiss_make_ack(&r, NULL) == 0);
    CHECK(sc_airkiss_feed(NULL, 1) == -1); unavailable(NULL);
    sc_airkiss_reset(NULL); sc_airkiss_destroy(NULL);
    CHECK(sc_airkiss_capture_feed(NULL, NULL, 0, 0, 0, 0) == -1);
    CHECK(sc_airkiss_capture_tick(NULL, 0) == -1 && sc_airkiss_capture_get_state(NULL) == -1);
    CHECK(sc_airkiss_capture_get_lock(NULL, NULL) == 0 && sc_airkiss_capture_get_result(NULL, NULL) == 0);
    sc_airkiss_capture_reset(NULL); sc_airkiss_capture_destroy(NULL);
}
int main(void)
{
    test_all_lengths(); test_recovery(); test_conflicts_and_integrity(); test_invalid_metadata_bounds(); test_capture(); test_guide_permutations(); test_null_and_ack();
    puts("PASS: AirKiss all field lengths/tails, reorder/repeat/drop/recovery, integrity/conflicts, independent contexts, capture framing/sources/timeouts, ack");
    return 0;
}
