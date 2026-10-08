/* Behavioral mocks authored from the capture contract, not SDK source/ABI. */
#include "sc_touch2_idf.h"
#include "sc_touch2_psa.h"
#include "sc_touch.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
enum { GET_MODE, GET_AP, GET_PROMISC, GET_COUNTRY, GET_CHANNEL, GET_FILTER,
       SET_FILTER, SET_CALLBACK, SET_CHANNEL, SET_PROMISC, API_COUNT };
static struct {
    wifi_mode_t mode;
    bool associated, promiscuous;
    wifi_country_t country;
    uint8_t channel;
    wifi_second_chan_t secondary;
    wifi_promiscuous_filter_t filter;
    wifi_promiscuous_cb_t callback;
    int64_t microseconds;
    unsigned int fail[API_COUNT], calls[API_COUNT], posts[2], delivered[2];
    int fail_event;
    sc_touch2_result result;
    sc_capture_lock lock;
} mock;

static esp_err_t api(unsigned int operation)
{
    ++mock.calls[operation];
    if (mock.fail[operation] != 0) { --mock.fail[operation]; return ESP_FAIL; }
    return ESP_OK;
}
void test_enter_critical(portMUX_TYPE *mux) { CHECK(*mux == 0); *mux = 1; }
void test_exit_critical(portMUX_TYPE *mux) { CHECK(*mux == 1); *mux = 0; }
int64_t esp_timer_get_time(void) { return mock.microseconds; }
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode)
{ esp_err_t e = api(GET_MODE); if (e == ESP_OK) *mode = mock.mode; return e; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *record)
{ esp_err_t e = api(GET_AP); (void)record; return e != ESP_OK ? e : (mock.associated ? ESP_OK : ESP_ERR_WIFI_NOT_CONNECT); }
esp_err_t esp_wifi_get_promiscuous(bool *enabled)
{ esp_err_t e = api(GET_PROMISC); if (e == ESP_OK) *enabled = mock.promiscuous; return e; }
esp_err_t esp_wifi_get_country(wifi_country_t *country)
{ esp_err_t e = api(GET_COUNTRY); if (e == ESP_OK) *country = mock.country; return e; }
esp_err_t esp_wifi_get_channel(uint8_t *channel, wifi_second_chan_t *secondary)
{ esp_err_t e = api(GET_CHANNEL); if (e == ESP_OK) { *channel = mock.channel; *secondary = mock.secondary; } return e; }
esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *filter)
{ esp_err_t e = api(GET_FILTER); if (e == ESP_OK) *filter = mock.filter; return e; }
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter)
{ esp_err_t e = api(SET_FILTER); if (e == ESP_OK) mock.filter = *filter; return e; }
esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t callback)
{ esp_err_t e = api(SET_CALLBACK); if (e == ESP_OK) mock.callback = callback; return e; }
esp_err_t esp_wifi_set_channel(uint8_t channel, wifi_second_chan_t secondary)
{ esp_err_t e = api(SET_CHANNEL); if (e == ESP_OK) { mock.channel = channel; mock.secondary = secondary; } return e; }
esp_err_t esp_wifi_set_promiscuous(bool enabled)
{ esp_err_t e = api(SET_PROMISC); if (e == ESP_OK) mock.promiscuous = enabled; return e; }
esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void *data, size_t bytes, unsigned int wait)
{
    CHECK(base == SC_TOUCH2_EVENT && id >= 0 && id <= 1 && wait == 0);
    ++mock.posts[id];
    if (mock.fail_event == id) { mock.fail_event = -1; return ESP_ERR_TIMEOUT; }
    if (id == SC_TOUCH2_GOT_CREDENTIALS) {
        CHECK(!mock.promiscuous && bytes == sizeof(mock.result));
        memcpy(&mock.result, data, bytes);
    } else {
        CHECK(bytes == sizeof(mock.lock));
        memcpy(&mock.lock, data, bytes);
    }
    ++mock.delivered[id];
    return ESP_OK;
}
static void reset_mock(void)
{
    CHECK(sc_touch2_idf_stop() == ESP_OK);
    memset(&mock, 0, sizeof(mock));
    mock.mode = WIFI_MODE_STA;
    mock.country.schan = 1; mock.country.nchan = 11;
    mock.channel = 7; mock.secondary = WIFI_SECOND_CHAN_ABOVE;
    mock.filter.filter_mask = 99;
    mock.fail_event = -1;
}
static void check_restored(void)
{
    CHECK(!mock.promiscuous && mock.callback == NULL);
    CHECK(mock.filter.filter_mask == 99 && mock.channel == 7);
    CHECK(mock.secondary == WIFI_SECOND_CHAN_ABOVE);
}
static void test_start_and_rollback(void)
{
    unsigned int operation;
    reset_mock(); mock.associated = true;
    CHECK(sc_touch2_idf_start(NULL) == ESP_ERR_INVALID_STATE);
    reset_mock(); mock.promiscuous = true;
    CHECK(sc_touch2_idf_start(NULL) == ESP_ERR_INVALID_STATE);
    mock.promiscuous = false;
    reset_mock(); mock.mode = WIFI_MODE_NULL;
    CHECK(sc_touch2_idf_start(NULL) == ESP_ERR_INVALID_STATE);
    reset_mock(); mock.country.nchan = 0;
    CHECK(sc_touch2_idf_start(NULL) == ESP_ERR_INVALID_STATE);
    reset_mock(); mock.country.schan = 15;
    CHECK(sc_touch2_idf_start(NULL) == ESP_ERR_INVALID_STATE);
    for (operation = 0; operation < API_COUNT; ++operation) {
        reset_mock(); mock.fail[operation] = 1;
        CHECK(sc_touch2_idf_start(NULL) == ESP_FAIL);
        check_restored();
        CHECK(sc_touch2_idf_poll() == ESP_ERR_INVALID_STATE);
        CHECK(sc_touch2_idf_stop() == ESP_OK);
        CHECK(sc_touch2_idf_start(NULL) == ESP_OK);
        CHECK(sc_touch2_idf_start(NULL) == ESP_ERR_INVALID_STATE);
        CHECK(sc_touch2_idf_stop() == ESP_OK);
        check_restored();
    }
}
static uint16_t sequence;
static const uint8_t test_bssid[6] = {2, 1, 2, 3, 4, 5};
static void packet_make(wifi_promiscuous_pkt_t *packet, uint16_t length)
{
    uint16_t seq = (uint16_t)((sequence++ & 4095U) << 4);
    memset(packet, 0, sizeof(*packet));
    packet->rx_ctrl.sig_len = (uint16_t)(length + 100U);
    packet->rx_ctrl.channel = mock.channel;
    packet->payload[0] = 8; packet->payload[1] = 1;
    memcpy(packet->payload + 4, test_bssid, 6);
    packet->payload[10] = 4;
    memset(packet->payload + 16, 255, 6);
    packet->payload[22] = (uint8_t)seq;
    packet->payload[23] = (uint8_t)(seq >> 8);
}
static void queue_length(uint16_t length)
{
    wifi_promiscuous_pkt_t packet;
    packet_make(&packet, length);
    mock.microseconds += 1000;
    CHECK(mock.callback != NULL);
    mock.callback(&packet, WIFI_PKT_DATA);
}
static void queue_guides(void)
{
    unsigned int i;
    for (i = 0; i < 4; ++i) queue_length((uint16_t)(i % 2U ? 1075U : 1048U));
}
static void queue_planes(const uint8_t bytes[6])
{
    size_t i, k;
    for (i = 0; i < 8; ++i) {
        unsigned int data = 0;
        for (k = 0; k < 6; ++k) data |= (((unsigned int)bytes[k] >> i) & 1U) << (5U - k);
        queue_length((uint16_t)(64U | (i << 7) | data));
    }
}
static void queue_credentials(void)
{
    uint8_t header[6] = {129, 129, 129, 0, 1, 0};
    uint8_t group[6] = {255, 85, 85, 85, 85, 0};
    size_t i;
    header[3] = sc_touch_crc8(test_bssid, 6);
    header[5] = sc_touch_crc8(header, 5);
    queue_planes(header);
    for (i = 0; i < 3; ++i) {
        group[0] = (uint8_t)(i == 0 ? 255 : i == 1 ? 131 : 0);
        group[5] = sc_touch_crc8(group, 5);
        queue_length((uint16_t)(128U + i));
        queue_planes(group);
    }
}
/* Lifecycle mock only: real PSA implementation is tested separately. */
int sc_touch2_psa_decrypt(void *user, const uint8_t key[16], const uint8_t iv[16],
                         const uint8_t *cipher, size_t length, uint8_t *plain)
{
    (void)user; (void)key; (void)iv; (void)cipher; (void)length; (void)plain;
    return 0;
}
static void test_events_stop_and_association(void)
{
    wifi_promiscuous_cb_t old_callback;
    wifi_promiscuous_pkt_t old_packet;
    reset_mock(); CHECK(sc_touch2_idf_start(NULL) == ESP_OK);
    old_callback = mock.callback;
    packet_make(&old_packet, 515);
    queue_guides(); mock.fail_event = SC_TOUCH2_FOUND_CHANNEL;
    CHECK(sc_touch2_idf_poll() == ESP_ERR_TIMEOUT);
    CHECK(mock.delivered[0] == 0);
    CHECK(sc_touch2_idf_poll() == ESP_OK && mock.delivered[0] == 1);
    queue_credentials(); mock.fail_event = SC_TOUCH2_GOT_CREDENTIALS;
    CHECK(sc_touch2_idf_poll() == ESP_ERR_TIMEOUT && !mock.promiscuous);
    CHECK(mock.delivered[1] == 0);
    CHECK(sc_touch2_idf_poll() == ESP_OK && mock.delivered[1] == 1);
    CHECK(sc_touch2_idf_poll() == ESP_OK && mock.delivered[1] == 1);
    CHECK(mock.posts[0] == 2 && mock.posts[1] == 2);
    CHECK(mock.result.password_len == 1 && mock.result.password[0] == 255);
    CHECK(sc_touch2_idf_stop() == ESP_OK);
    check_restored();
    old_callback(&old_packet, WIFI_PKT_DATA); /* Static ring is safe after stop. */
    CHECK(sc_touch2_idf_poll() == ESP_ERR_INVALID_STATE);
    reset_mock(); CHECK(sc_touch2_idf_start(NULL) == ESP_OK);
    queue_guides(); queue_credentials(); CHECK(sc_touch2_idf_poll() == ESP_OK);
    CHECK(mock.delivered[1] == 1);
    mock.associated = true; mock.channel = 11;
    CHECK(sc_touch2_idf_stop() == ESP_OK);
    CHECK(mock.channel == 11 && mock.callback == NULL && !mock.promiscuous);
    CHECK(mock.filter.filter_mask == 99);
}
static void test_hop_and_stop_retry(void)
{
    unsigned int operation;
    reset_mock(); mock.country.schan = 13; mock.country.nchan = 5;
    CHECK(sc_touch2_idf_start(NULL) == ESP_OK && mock.channel == 13);
    mock.microseconds = 350000;
    CHECK(sc_touch2_idf_poll() == ESP_OK && mock.channel == 14);
    mock.microseconds = 700000;
    CHECK(sc_touch2_idf_poll() == ESP_OK && mock.channel == 13);
    CHECK(sc_touch2_idf_stop() == ESP_OK); check_restored();
    for (operation = SET_FILTER; operation < API_COUNT; ++operation) {
        reset_mock(); CHECK(sc_touch2_idf_start(NULL) == ESP_OK);
        mock.fail[operation] = 1;
        CHECK(sc_touch2_idf_stop() == ESP_FAIL);
        CHECK(sc_touch2_idf_poll() == ESP_ERR_INVALID_STATE);
        CHECK(sc_touch2_idf_start(NULL) == ESP_ERR_INVALID_STATE);
        CHECK(sc_touch2_idf_stop() == ESP_OK); check_restored();
    }
}
int main(void)
{
    test_start_and_rollback();
    test_events_stop_and_association();
    test_hop_and_stop_retry();
    puts("PASS: v2 IDF lifecycle mocks, start refusals/API rollback, event retries, completion disable, late callback, association-safe stop, hopping, cleanup retries");
    return 0;
}
