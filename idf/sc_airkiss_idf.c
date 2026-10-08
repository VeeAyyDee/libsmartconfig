#include "sc_airkiss_idf.h"
#include "sc_touch2_psa.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

ESP_EVENT_DEFINE_BASE(SC_AIRKISS_EVENT);
enum { RING_SIZE = 128, HEADER_SIZE = 26, HOP_MS = 350 };
typedef struct {
    uint8_t header[HEADER_SIZE];
    uint8_t header_bytes, channel;
    uint16_t wire_length;
    uint32_t time_ms;
} captured_frame;
static portMUX_TYPE ring_mux = portMUX_INITIALIZER_UNLOCKED;
static captured_frame ring[RING_SIZE];
static size_t ring_head, ring_tail, ring_count;
static bool ring_accepting;
static struct {
    sc_airkiss_capture *capture;
    bool active, running, callback_installed, promiscuous_enabled;
    bool filter_changed, channel_changed;
    bool found_delivered, credentials_delivered;
    wifi_promiscuous_filter_t saved_filter;
    uint8_t saved_channel, channel, first_channel, last_channel;
    wifi_second_chan_t saved_secondary;
    uint32_t hop_at;
    int last_state;
} adapter;

static uint32_t clock_ms(void)
{
    return (uint32_t)((uint64_t)esp_timer_get_time() / UINT64_C(1000));
}

static void ring_enable(bool enable)
{
    portENTER_CRITICAL(&ring_mux);
    ring_accepting = enable;
    ring_head = 0;
    ring_tail = 0;
    ring_count = 0;
    portEXIT_CRITICAL(&ring_mux);
}

static void receive_frame(void *buffer, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *packet = buffer;
    uint16_t wire_length;
    size_t header_bytes;
    uint32_t timestamp;
    if (type != WIFI_PKT_DATA || packet == NULL || packet->rx_ctrl.rx_state != 0)
        return;
    wire_length = (uint16_t)packet->rx_ctrl.sig_len;
    header_bytes = wire_length < HEADER_SIZE ? wire_length : HEADER_SIZE;
    timestamp = clock_ms();
    portENTER_CRITICAL(&ring_mux);
    if (ring_accepting && ring_count < RING_SIZE) {
        captured_frame *frame = &ring[ring_head];
        memcpy(frame->header, packet->payload, header_bytes);
        frame->header_bytes = (uint8_t)header_bytes;
        frame->wire_length = wire_length;
        frame->channel = (uint8_t)packet->rx_ctrl.channel;
        frame->time_ms = timestamp;
        ring_head = (ring_head + 1U) % RING_SIZE;
        ++ring_count;
    }
    portEXIT_CRITICAL(&ring_mux);
}

static bool ring_pop(captured_frame *frame)
{
    bool found = false;
    portENTER_CRITICAL(&ring_mux);
    if (ring_count != 0) {
        *frame = ring[ring_tail];
        ring_tail = (ring_tail + 1U) % RING_SIZE;
        --ring_count;
        found = true;
    }
    portEXIT_CRITICAL(&ring_mux);
    return found;
}

static void first_error(esp_err_t *result, esp_err_t error)
{
    if (*result == ESP_OK && error != ESP_OK) *result = error;
}

/* Keep failed restoration flags so stop can retry a partial rollback. */
esp_err_t sc_airkiss_idf_stop(void)
{
    esp_err_t result = ESP_OK, error;
    wifi_ap_record_t associated;
    if (!adapter.active) return ESP_OK;
    adapter.running = false;
    ring_enable(false);
    if (adapter.promiscuous_enabled) {
        error = esp_wifi_set_promiscuous(false);
        first_error(&result, error);
        if (error == ESP_OK) adapter.promiscuous_enabled = false;
    }
    if (adapter.callback_installed) {
        error = esp_wifi_set_promiscuous_rx_cb(NULL);
        first_error(&result, error);
        if (error == ESP_OK) adapter.callback_installed = false;
    }
    if (adapter.filter_changed) {
        error = esp_wifi_set_promiscuous_filter(&adapter.saved_filter);
        first_error(&result, error);
        if (error == ESP_OK) adapter.filter_changed = false;
    }
    if (adapter.channel_changed) {
        error = esp_wifi_sta_get_ap_info(&associated);
        if (error == ESP_OK) {
            /* Preserve a live association established after credentials. */
            adapter.channel_changed = false;
        } else if (error == ESP_ERR_WIFI_NOT_CONNECT) {
            error = esp_wifi_set_channel(adapter.saved_channel, adapter.saved_secondary);
            first_error(&result, error);
            if (error == ESP_OK) adapter.channel_changed = false;
        } else {
            first_error(&result, error);
        }
    }
    if (!adapter.promiscuous_enabled && !adapter.callback_installed &&
        !adapter.filter_changed && !adapter.channel_changed) {
        sc_airkiss_capture_destroy(adapter.capture);
        memset(&adapter, 0, sizeof(adapter));
    }
    return result;
}

static esp_err_t start_with_config(const sc_airkiss_config *config)
{
    esp_err_t error, rollback;
    wifi_mode_t mode;
    wifi_ap_record_t associated;
    wifi_country_t country;
    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
    wifi_promiscuous_filter_t saved_filter;
    wifi_second_chan_t secondary;
    uint8_t saved_channel;
    bool enabled;
    unsigned int first, last;
    if (adapter.active) return ESP_ERR_INVALID_STATE;
    error = esp_wifi_get_mode(&mode);
    if (error != ESP_OK) return error;
    if (mode != WIFI_MODE_STA) return ESP_ERR_INVALID_STATE;
    error = esp_wifi_sta_get_ap_info(&associated);
    if (error == ESP_OK) return ESP_ERR_INVALID_STATE;
    if (error != ESP_ERR_WIFI_NOT_CONNECT) return error;
    error = esp_wifi_get_promiscuous(&enabled);
    if (error != ESP_OK) return error;
    if (enabled) return ESP_ERR_INVALID_STATE;
    error = esp_wifi_get_country(&country);
    if (error != ESP_OK) return error;
    if (country.nchan == 0) return ESP_ERR_INVALID_STATE;
    first = country.schan;
    last = first + (unsigned int)country.nchan - 1U;
    if (first < 1U) first = 1U;
    if (last > 14U) last = 14U;
    if (first > last) return ESP_ERR_INVALID_STATE;
    error = esp_wifi_get_channel(&saved_channel, &secondary);
    if (error != ESP_OK) return error;
    error = esp_wifi_get_promiscuous_filter(&saved_filter);
    if (error != ESP_OK) return error;
    adapter.capture = sc_airkiss_capture_create_with_config(config);
    if (adapter.capture == NULL) return ESP_ERR_NO_MEM;
    adapter.active = true;
    adapter.saved_channel = saved_channel;
    adapter.saved_secondary = secondary;
    adapter.saved_filter = saved_filter;
    adapter.first_channel = (uint8_t)first;
    adapter.last_channel = (uint8_t)last;
    adapter.channel = (uint8_t)first;
    ring_enable(false);
    error = esp_wifi_set_promiscuous_filter(&filter);
    if (error != ESP_OK) goto fail;
    adapter.filter_changed = true;
    error = esp_wifi_set_promiscuous_rx_cb(receive_frame);
    if (error != ESP_OK) goto fail;
    adapter.callback_installed = true;
    error = esp_wifi_set_channel(adapter.channel, WIFI_SECOND_CHAN_NONE);
    if (error != ESP_OK) goto fail;
    adapter.channel_changed = true;
    ring_enable(true);
    error = esp_wifi_set_promiscuous(true);
    if (error != ESP_OK) goto fail;
    adapter.promiscuous_enabled = true;
    adapter.hop_at = clock_ms();
    adapter.running = true;
    return ESP_OK;
fail:
    rollback = sc_airkiss_idf_stop();
    return rollback != ESP_OK ? rollback : error;
}

esp_err_t sc_airkiss_idf_start(void) { return start_with_config(NULL); }

esp_err_t sc_airkiss_idf_start_with_key(const uint8_t *key, size_t key_len)
{
    sc_airkiss_config config = {0};
    esp_err_t result;
    volatile uint8_t *bytes;
    size_t i;
    if (key == NULL || key_len == 0 || key_len > sizeof(config.key)) return ESP_ERR_INVALID_ARG;
    memcpy(config.key, key, key_len);
    config.key_len = key_len;
    config.decrypt = sc_touch2_psa_decrypt;
    result = start_with_config(&config);
    bytes = (volatile uint8_t *)&config;
    for (i = 0; i < sizeof(config); ++i) bytes[i] = 0;
    return result;
}

static void observe_state(int state)
{
    if (state == 0 && adapter.last_state != 0) {
        adapter.found_delivered = false;
        adapter.credentials_delivered = false;
    }
    adapter.last_state = state;
}

esp_err_t sc_airkiss_idf_poll(void)
{
    captured_frame frame;
    sc_capture_lock lock;
    sc_airkiss_result result;
    uint32_t now;
    size_t work;
    int state;
    esp_err_t error;
    if (!adapter.running || adapter.capture == NULL) return ESP_ERR_INVALID_STATE;
    for (work = 0; work < RING_SIZE && ring_pop(&frame); ++work) {
        if (frame.channel != adapter.channel) continue;
        state = sc_airkiss_capture_feed(adapter.capture, frame.header, frame.header_bytes,
                                frame.wire_length, frame.channel, frame.time_ms);
        if (state < 0) return ESP_ERR_INVALID_STATE;
        observe_state(state);
    }
    now = clock_ms();
    state = sc_airkiss_capture_tick(adapter.capture, now);
    if (state < 0) return ESP_ERR_INVALID_STATE;
    observe_state(state);
    if (state == 0) {
        if ((uint32_t)(now - adapter.hop_at) >= HOP_MS) {
            uint8_t next = adapter.channel >= adapter.last_channel ?
                           adapter.first_channel : (uint8_t)(adapter.channel + 1U);
            error = esp_wifi_set_channel(next, WIFI_SECOND_CHAN_NONE);
            if (error != ESP_OK) return error;
            adapter.channel = next;
            adapter.channel_changed = true;
            adapter.hop_at = now;
        }
        return ESP_OK;
    }
    if (state == 2 && adapter.promiscuous_enabled) {
        ring_enable(false);
        error = esp_wifi_set_promiscuous(false);
        if (error != ESP_OK) return error;
        adapter.promiscuous_enabled = false;
    }
    if (!adapter.found_delivered) {
        if (!sc_airkiss_capture_get_lock(adapter.capture, &lock)) return ESP_ERR_INVALID_STATE;
        error = esp_event_post(SC_AIRKISS_EVENT, SC_AIRKISS_FOUND_CHANNEL, &lock, sizeof(lock), 0);
        if (error != ESP_OK) return error;
        adapter.found_delivered = true;
    }
    if (state == 2 && !adapter.credentials_delivered) {
        if (!sc_airkiss_capture_get_result(adapter.capture, &result)) return ESP_ERR_INVALID_STATE;
        error = esp_event_post(SC_AIRKISS_EVENT, SC_AIRKISS_GOT_CREDENTIALS, &result, sizeof(result), 0);
        if (error != ESP_OK) return error;
        adapter.credentials_delivered = true;
    }
    return ESP_OK;
}
