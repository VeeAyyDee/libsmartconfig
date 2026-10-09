/* SPDX-License-Identifier: 0BSD */
/* Original receiver for the SDK's public wrapper. No SDK wrapper/ACK copies. */
#include "esp_smartconfig.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sc_capture.h"
#include "sc_airkiss_capture.h"
#include "sc_touch2_capture.h"
#include "sc_touch2_psa.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum { RING_SIZE = 128, HEADER_SIZE = 26, STOP_WAIT_MS = 2000, AP_LIMIT = 64 };
typedef struct {
    uint8_t header[HEADER_SIZE], header_bytes, channel;
    uint16_t length;
    uint32_t time;
} frame_t;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static frame_t ring[RING_SIZE];
static size_t head, tail, count;
/* All fields below this comment through api_busy are protected by mux. */
static bool accepting, stop_requested, worker_running, api_busy;
static TaskHandle_t worker_handle;
static smartconfig_type_t selected_type = SC_TYPE_ESPTOUCH_AIRKISS;
static uint8_t timeout_seconds = 15;
static uint32_t dwell_ms = 150, generation;
ESP_EVENT_DEFINE_BASE(SC_DISCOVERY_EVENT);
/* Only these mailbox fields are shared with SDK event callbacks. */
static bool scan_waiting, scan_done, fence_done;
static uint32_t scan_status;
static struct {
    sc_capture *v1;
    sc_airkiss_capture *airkiss;
    sc_touch2_capture *v2;
    bool active, callback, promiscuous, filter_changed, channel_changed;
    bool scan_owned, scan_handler, fence_handler, discovering, scan_posted, fence_posted;
    esp_event_handler_instance_t scan_instance, fence_instance;
    wifi_ap_record_t aps[AP_LIMIT];
    size_t ap_count;
    unsigned int scan_pass;
    uint16_t channels;
    uint32_t retry_at, dwell;
    bool found_posted, credentials_posted, result_ready, logging;
    wifi_promiscuous_filter_t saved_filter;
    uint8_t saved_channel, channel, first, last;
    wifi_second_chan_t saved_secondary;
    uint32_t hop_at, attempt_at;
    smartconfig_type_t winner;
    sc_capture_lock lock;
    smartconfig_event_got_ssid_pswd_t event;
    uint8_t reserved[64], reserved_len;
} receiver;

static uint32_t now_ms(void) { return (uint32_t)((uint64_t)esp_timer_get_time() / UINT64_C(1000)); }
static void wipe(void *p, size_t size)
{ volatile uint8_t *b = p; while (size != 0) { *b++ = 0; --size; } }
static bool enter_api(void)
{
    bool okay;
    portENTER_CRITICAL(&mux); okay = !api_busy; if (okay) api_busy = true; portEXIT_CRITICAL(&mux);
    return okay;
}
static void leave_api(void) { portENTER_CRITICAL(&mux); api_busy = false; portEXIT_CRITICAL(&mux); }
static bool stopping(void)
{ bool value; portENTER_CRITICAL(&mux); value = stop_requested; portEXIT_CRITICAL(&mux); return value; }
static void ring_enable(bool enabled)
{ portENTER_CRITICAL(&mux); accepting = enabled; head = tail = count = 0; portEXIT_CRITICAL(&mux); }
static bool pop(frame_t *frame)
{
    bool found = false;
    portENTER_CRITICAL(&mux);
    if (count != 0) { *frame = ring[tail]; tail = (tail + 1U) % RING_SIZE; --count; found = true; }
    portEXIT_CRITICAL(&mux); return found;
}
static void receive_frame(void *buffer, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *packet = buffer;
    size_t bytes;
    uint32_t time;
    if (packet == NULL || type != WIFI_PKT_DATA || packet->rx_ctrl.rx_state != 0) return;
    bytes = packet->rx_ctrl.sig_len < HEADER_SIZE ? packet->rx_ctrl.sig_len : HEADER_SIZE;
    time = now_ms();
    portENTER_CRITICAL(&mux);
    if (accepting && count < RING_SIZE) {
        frame_t *frame = &ring[head];
        memcpy(frame->header, packet->payload, bytes);
        frame->header_bytes = (uint8_t)bytes; frame->length = (uint16_t)packet->rx_ctrl.sig_len;
        frame->channel = (uint8_t)packet->rx_ctrl.channel; frame->time = time;
        head = (head + 1U) % RING_SIZE; ++count;
    }
    portEXIT_CRITICAL(&mux);
}
static void reset_attempt(uint32_t time)
{
    if (receiver.v1 != NULL) sc_capture_reset(receiver.v1);
    if (receiver.airkiss != NULL) sc_airkiss_capture_reset(receiver.airkiss);
    if (receiver.v2 != NULL) sc_touch2_capture_reset(receiver.v2);
    receiver.found_posted = false;
    portENTER_CRITICAL(&mux);
    receiver.result_ready = false; receiver.winner = SC_TYPE_ESPTOUCH_AIRKISS;
    portEXIT_CRITICAL(&mux);
    receiver.attempt_at = time; receiver.hop_at = time; receiver.dwell = dwell_ms;
    memset(&receiver.lock, 0, sizeof(receiver.lock));
    ring_enable(true);
}
/* A private event-loop fence drains earlier queued completion events before
 * an owned scan begins, including completion queued before a stop/restart. */
static void discovery_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)id;
    portENTER_CRITICAL(&mux);
    if (!stop_requested) {
        if (base == SC_DISCOVERY_EVENT && data != NULL && *(uint32_t *)data == generation)
            fence_done = true;
        else if (base == WIFI_EVENT && data != NULL && scan_waiting) {
            scan_status = ((wifi_event_sta_scan_done_t *)data)->status;
            scan_done = true; scan_waiting = false;
        }
    }
    portEXIT_CRITICAL(&mux);
}
static void report_error(const char *operation, esp_err_t error)
{
    if (receiver.logging && error != ESP_OK)
        fprintf(stderr, "smartconfig: %s failed (%ld)\n", operation, (long)error);
}
static void begin_discovery(uint32_t time)
{
    ring_enable(false);
    reset_attempt(time);
    ring_enable(false);
    receiver.discovering = true; receiver.scan_posted = false;
    receiver.fence_posted = false; receiver.ap_count = 0; receiver.channels = 0;
    receiver.scan_pass = 0; receiver.retry_at = time;
    portENTER_CRITICAL(&mux);
    ++generation; scan_waiting = scan_done = fence_done = false;
    portEXIT_CRITICAL(&mux);
}
static uint8_t next_channel(uint8_t current)
{
    unsigned int i;
    for (i = 0; i < 14; ++i) {
        current = current >= 14 ? 1 : (uint8_t)(current + 1U);
        if ((receiver.channels & (uint16_t)(1U << current)) != 0) return current;
    }
    return receiver.first;
}
static void discover(uint32_t time)
{
    bool completed, fenced;
    uint32_t status;
    esp_err_t error;
    wifi_scan_config_t config = {0};
    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
    if (receiver.promiscuous) {
        if (esp_wifi_set_promiscuous(false) != ESP_OK) return;
        receiver.promiscuous = false;
    }
    portENTER_CRITICAL(&mux);
    completed = scan_done; scan_done = false; status = scan_status; fenced = fence_done;
    portEXIT_CRITICAL(&mux);
    if (completed) {
        uint16_t number = 0, i;
        error = status == 0 ? esp_wifi_scan_get_ap_num(&number) : ESP_FAIL;
        for (i = 0; error == ESP_OK && i < number; ++i) {
            wifi_ap_record_t ap;
            size_t j;
            error = esp_wifi_scan_get_ap_record(&ap);
            if (error != ESP_OK) break;
            if (ap.rssi <= -85 || ap.primary < receiver.first || ap.primary > receiver.last) continue;
            for (j = 0; j < receiver.ap_count; ++j)
                if (memcmp(receiver.aps[j].bssid, ap.bssid, 6) == 0) break;
            if (j == receiver.ap_count && receiver.ap_count < AP_LIMIT) receiver.aps[receiver.ap_count++] = ap;
            else if (j < receiver.ap_count && (memcmp(receiver.aps[j].ssid, ap.ssid, 32) != 0 ||
                     receiver.aps[j].primary != ap.primary || receiver.aps[j].pairwise_cipher != ap.pairwise_cipher))
                memset(receiver.aps[j].ssid, 0, sizeof(receiver.aps[j].ssid));
            receiver.channels |= (uint16_t)(1U << ap.primary);
        }
        if (esp_wifi_clear_ap_list() != ESP_OK) {
            portENTER_CRITICAL(&mux); scan_status = 1; scan_done = true; portEXIT_CRITICAL(&mux);
            return;
        }
        receiver.scan_owned = false;
        report_error("scan results", error);
        if (error == ESP_OK) ++receiver.scan_pass;
        receiver.retry_at = time + (error == ESP_OK && receiver.ap_count != 0 ? 0U : 50U);
    }
    if ((int32_t)(time - receiver.retry_at) < 0) return;
    if (receiver.scan_pass >= 2 && receiver.ap_count != 0 && receiver.channels != 0) {
        if (!receiver.filter_changed) {
            if (esp_wifi_set_promiscuous_filter(&filter) != ESP_OK) return;
            receiver.filter_changed = true;
        }
        if (!receiver.callback) {
            if (esp_wifi_set_promiscuous_rx_cb(receive_frame) != ESP_OK) return;
            receiver.callback = true;
        }
        receiver.channel = next_channel(14);
        error = esp_wifi_set_channel(receiver.channel, WIFI_SECOND_CHAN_NONE);
        if (error != ESP_OK) { report_error("initial channel", error); receiver.retry_at = time + 50U; return; }
        receiver.channel_changed = true;
        error = esp_wifi_set_promiscuous(true);
        if (error != ESP_OK) { report_error("capture enable", error); receiver.retry_at = time + 50U; return; }
        receiver.promiscuous = true;
        reset_attempt(time);
        if (stopping()) return;
        if (esp_event_post(SC_EVENT, SC_EVENT_SCAN_DONE, NULL, 0, 0) != ESP_OK) { ring_enable(false); return; }
        receiver.scan_posted = true; receiver.discovering = false;
        return;
    }
    if (receiver.scan_owned || (int32_t)(time - receiver.retry_at) < 0) return;
    if (!receiver.fence_posted) {
        if (esp_event_post(SC_DISCOVERY_EVENT, 0, &generation, sizeof(generation), 0) == ESP_OK)
            receiver.fence_posted = true;
        return;
    }
    if (!fenced || stopping()) return;
    config.show_hidden = true;
    portENTER_CRITICAL(&mux); scan_waiting = true; portEXIT_CRITICAL(&mux);
    error = esp_wifi_scan_start(&config, false);
    if (error == ESP_OK) receiver.scan_owned = true;
    else {
        report_error("scan start", error);
        portENTER_CRITICAL(&mux); scan_waiting = scan_done = false; portEXIT_CRITICAL(&mux);
        receiver.retry_at = time + 50U;
    }
}
static void remember_error(esp_err_t *first, esp_err_t error)
{ if (*first == ESP_OK && error != ESP_OK) *first = error; }
/* Called with exclusive API ownership after the worker has relinquished state. */
static esp_err_t cleanup(void)
{
    esp_err_t result = ESP_OK, error;
    wifi_ap_record_t associated;
    ring_enable(false);
    portENTER_CRITICAL(&mux); scan_waiting = scan_done = fence_done = false; portEXIT_CRITICAL(&mux);
    if (receiver.scan_owned) {
        error = esp_wifi_scan_stop(); remember_error(&result, error);
        if (error == ESP_OK) {
            error = esp_wifi_clear_ap_list(); remember_error(&result, error);
            if (error == ESP_OK) receiver.scan_owned = false;
        }
    }
    if (receiver.scan_handler) {
        error = esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, receiver.scan_instance);
        remember_error(&result, error); if (error == ESP_OK) receiver.scan_handler = false;
    }
    if (receiver.fence_handler) {
        error = esp_event_handler_instance_unregister(SC_DISCOVERY_EVENT, 0, receiver.fence_instance);
        remember_error(&result, error); if (error == ESP_OK) receiver.fence_handler = false;
    }
    if (receiver.promiscuous) {
        error = esp_wifi_set_promiscuous(false); remember_error(&result, error);
        if (error == ESP_OK) receiver.promiscuous = false;
    }
    if (receiver.callback) {
        error = esp_wifi_set_promiscuous_rx_cb(NULL); remember_error(&result, error);
        if (error == ESP_OK) receiver.callback = false;
    }
    if (receiver.filter_changed) {
        error = esp_wifi_set_promiscuous_filter(&receiver.saved_filter); remember_error(&result, error);
        if (error == ESP_OK) receiver.filter_changed = false;
    }
    if (receiver.channel_changed) {
        error = esp_wifi_sta_get_ap_info(&associated);
        if (error == ESP_OK) receiver.channel_changed = false;
        else if (error == ESP_ERR_WIFI_NOT_CONNECT) {
            error = esp_wifi_set_channel(receiver.saved_channel, receiver.saved_secondary);
            remember_error(&result, error); if (error == ESP_OK) receiver.channel_changed = false;
        } else remember_error(&result, error);
    }
    if (!receiver.scan_owned && !receiver.scan_handler && !receiver.fence_handler && !receiver.promiscuous && !receiver.callback && !receiver.filter_changed && !receiver.channel_changed) {
        sc_capture_destroy(receiver.v1); sc_airkiss_capture_destroy(receiver.airkiss);
        sc_touch2_capture_destroy(receiver.v2); wipe(&receiver, sizeof(receiver));
    }
    return result;
}
const char *esp_smartconfig_get_version(void) { return "libsmartconfig 0.3.0"; }
esp_err_t esp_smartconfig_set_type(smartconfig_type_t type)
{
    esp_err_t result = ESP_OK;
    if (type < SC_TYPE_ESPTOUCH || type > SC_TYPE_ESPTOUCH_V2) return ESP_ERR_INVALID_ARG;
    if (!enter_api()) return ESP_ERR_INVALID_STATE;
    if (receiver.active) result = ESP_ERR_INVALID_STATE; else selected_type = type;
    leave_api(); return result;
}
esp_err_t esp_esptouch_set_timeout(uint8_t time_s)
{
    esp_err_t result = ESP_OK;
    if (time_s < 15) return ESP_ERR_INVALID_ARG;
    if (!enter_api()) return ESP_ERR_INVALID_STATE;
    if (receiver.active) result = ESP_ERR_INVALID_STATE; else timeout_seconds = time_s;
    leave_api(); return result;
}
esp_err_t esp_smartconfig_fast_mode(bool enabled)
{
    esp_err_t result;
    if (!enter_api()) return ESP_ERR_INVALID_STATE;
    result = receiver.active ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (result == ESP_OK) dwell_ms = enabled ? 50U : 100U;
    leave_api(); return result;
}
esp_err_t esp_smartconfig_get_rvd_data(uint8_t *data, uint8_t length)
{
    esp_err_t result = ESP_OK;
    if (data == NULL || length > 64) return ESP_ERR_INVALID_ARG;
    if (!enter_api()) return ESP_ERR_INVALID_STATE;
    /* Worker publishes readiness with mux; data is immutable after publication. */
    portENTER_CRITICAL(&mux);
    if (!receiver.result_ready || receiver.winner != SC_TYPE_ESPTOUCH_V2) result = ESP_ERR_INVALID_STATE;
    else {
        size_t copied = length < receiver.reserved_len ? length : receiver.reserved_len;
        memset(data, 0, length); memcpy(data, receiver.reserved, copied);
    }
    portEXIT_CRITICAL(&mux);
    leave_api(); return result;
}
static bool event_strings(const uint8_t *ssid, size_t sn, const uint8_t *pwd, size_t pn)
{
    /* Standard event carries strings with fixed maximum arrays, not counted binary. */
    if (sn == 0 || memchr(ssid, 0, sn) != NULL || memchr(pwd, 0, pn) != NULL) return false;
    memcpy(receiver.event.ssid, ssid, sn); memcpy(receiver.event.password, pwd, pn); return true;
}
static bool make_result(smartconfig_type_t type, const sc_capture_lock *lock)
{
    bool okay = false;
    memset(&receiver.event, 0, sizeof(receiver.event));
    if (type == SC_TYPE_ESPTOUCH) {
        sc_touch_result result;
        if (sc_capture_get_result(receiver.v1, &result)) {
            okay = event_strings(result.ssid, result.ssid_len, result.password, result.password_len);
            memcpy(receiver.event.bssid, result.bssid, 6);
            memcpy(receiver.event.cellphone_ip, result.sender_ip, 4);
            receiver.event.token = (uint8_t)(9U + result.ssid_len + result.password_len);
        }
        wipe(&result, sizeof(result));
    } else if (type == SC_TYPE_AIRKISS) {
        sc_airkiss_result result;
        if (sc_airkiss_capture_get_result(receiver.airkiss, &result)) {
            okay = event_strings(result.ssid, result.ssid_len, result.password, result.password_len);
            receiver.event.token = result.token; memcpy(receiver.event.bssid, lock->bssid, 6);
        }
        wipe(&result, sizeof(result));
    } else {
        sc_touch2_result result;
        if (sc_touch2_capture_get_result(receiver.v2, &result)) {
            okay = result.ipv4 != 0 && event_strings(result.ssid, result.ssid_len, result.password, result.password_len);
            receiver.event.token = result.port_mark; memcpy(receiver.event.bssid, lock->bssid, 6);
            memcpy(receiver.reserved, result.reserved, result.reserved_len); receiver.reserved_len = result.reserved_len;
        }
        wipe(&result, sizeof(result));
    }
    if (okay) {
        receiver.event.type = type; receiver.event.bssid_set = true;
        portENTER_CRITICAL(&mux);
        receiver.winner = type; receiver.result_ready = true;
        portEXIT_CRITICAL(&mux);
    }
    return okay;
}
static void apply_scan_hint(int protocol, const sc_capture_lock *lock)
{
    size_t i;
    for (i = 0; i < receiver.ap_count; ++i) {
        const wifi_ap_record_t *record = &receiver.aps[i];
        sc_scan_ap ap = {0};
        if (record->primary != lock->channel || memcmp(record->bssid, lock->bssid, 6) != 0 ||
            record->pairwise_cipher == WIFI_CIPHER_TYPE_UNKNOWN) continue;
        while (ap.ssid_len < 32 && record->ssid[ap.ssid_len] != 0) ++ap.ssid_len;
        if (ap.ssid_len == 0) return;
        memcpy(ap.ssid, record->ssid, ap.ssid_len); memcpy(ap.bssid, record->bssid, 6);
        ap.channel = record->primary; ap.protected_frame = record->pairwise_cipher != WIFI_CIPHER_TYPE_NONE;
        if (protocol == SC_TYPE_ESPTOUCH) (void)sc_capture_set_ap(receiver.v1, &ap);
        else if (protocol == SC_TYPE_AIRKISS) (void)sc_airkiss_capture_set_ap(receiver.airkiss, &ap);
        else (void)sc_touch2_capture_set_ap(receiver.v2, &ap);
        return;
    }
}
static int feed_protocol(int protocol, const frame_t *f, sc_capture_lock *lock)
{
    int state;
    if (protocol == SC_TYPE_ESPTOUCH) {
        if (receiver.v1 == NULL) return 0;
        state = f == NULL ? sc_capture_tick(receiver.v1, now_ms()) :
            sc_capture_feed(receiver.v1, f->header, f->header_bytes, f->length, f->channel, f->time);
        if (state > 0) (void)sc_capture_get_lock(receiver.v1, lock);
    } else if (protocol == SC_TYPE_AIRKISS) {
        if (receiver.airkiss == NULL) return 0;
        state = f == NULL ? sc_airkiss_capture_tick(receiver.airkiss, now_ms()) :
            sc_airkiss_capture_feed(receiver.airkiss, f->header, f->header_bytes, f->length, f->channel, f->time);
        if (state > 0) (void)sc_airkiss_capture_get_lock(receiver.airkiss, lock);
    } else {
        if (receiver.v2 == NULL) return 0;
        state = f == NULL ? sc_touch2_capture_tick(receiver.v2, now_ms()) :
            sc_touch2_capture_feed(receiver.v2, f->header, f->header_bytes, f->length, f->channel, f->time);
        if (state > 0) (void)sc_touch2_capture_get_lock(receiver.v2, lock);
    }
    if (state == 1) apply_scan_hint(protocol, lock);
    return state;
}
static void work(void)
{
    frame_t frame;
    sc_capture_lock lock = {0};
    uint32_t time = now_ms();
    size_t n;
    int protocol, locked = 0;
    if (receiver.credentials_posted) return;
    if (receiver.discovering) { discover(time); return; }
    if (!receiver.result_ready && (uint32_t)(time - receiver.attempt_at) >= ((uint32_t)timeout_seconds + 45U) * 1000U)
        { begin_discovery(time); return; }
    for (n = 0; n < RING_SIZE && !receiver.result_ready && pop(&frame); ++n) {
        if (stopping()) return;
        if (frame.channel != receiver.channel) continue;
        for (protocol = 0; protocol <= 3; ++protocol) {
            int state;
            if (protocol == SC_TYPE_ESPTOUCH_AIRKISS) continue;
            state = feed_protocol(protocol, &frame, &lock);
            if (state > 0) receiver.lock = lock;
            if (state == 2) {
                if (!make_result((smartconfig_type_t)protocol, &lock)) { begin_discovery(time); return; }
                break;
            }
        }
    }
    if (!receiver.result_ready) {
        for (protocol = 0; protocol <= 3; ++protocol) {
            int state;
            if (protocol == SC_TYPE_ESPTOUCH_AIRKISS) continue;
            state = feed_protocol(protocol, NULL, &lock);
            if (state > 0) { locked = 1; receiver.lock = lock; }
        }
    } else locked = 1;
    if (!locked) {
        receiver.found_posted = false;
        if ((uint32_t)(time - receiver.hop_at) >= receiver.dwell) {
            uint8_t next = next_channel(receiver.channel);
            receiver.hop_at = time; receiver.dwell = dwell_ms;
            esp_err_t retune = esp_wifi_set_channel(next, WIFI_SECOND_CHAN_NONE);
            report_error("channel hop", retune);
            if (retune == ESP_OK) {
                receiver.channel = next; receiver.channel_changed = true; receiver.hop_at = time;
                ring_enable(true);
            }
        }
        return;
    }
    if (receiver.result_ready && receiver.promiscuous) {
        ring_enable(false);
        if (esp_wifi_set_promiscuous(false) != ESP_OK) return;
        receiver.promiscuous = false;
    }
    if (stopping()) return;
    if (!receiver.found_posted) {
        if (esp_event_post(SC_EVENT, SC_EVENT_FOUND_CHANNEL, NULL, 0, 0) != ESP_OK) return;
        receiver.found_posted = true;
    }
    if (stopping()) return;
    if (receiver.result_ready && !receiver.credentials_posted) {
        if (esp_event_post(SC_EVENT, SC_EVENT_GOT_SSID_PSWD, &receiver.event, sizeof(receiver.event), 0) == ESP_OK)
            receiver.credentials_posted = true;
    }
}
static void worker(void *unused)
{
    (void)unused;
    while (!stopping()) { work(); vTaskDelay(pdMS_TO_TICKS(10)); }
    /* No shared receiver state is touched after this publication. */
    portENTER_CRITICAL(&mux); worker_running = false; portEXIT_CRITICAL(&mux);
    vTaskDelete(NULL);
}
esp_err_t esp_smartconfig_internal_stop(void)
{
    bool running;
    uint32_t start;
    esp_err_t result;
    if (!enter_api()) return ESP_ERR_INVALID_STATE;
    if (!receiver.active) { leave_api(); return ESP_OK; }
    portENTER_CRITICAL(&mux);
    stop_requested = true; accepting = false; running = worker_running;
    portEXIT_CRITICAL(&mux);
    /* Event delivery is nonblocking and happens in the SDK event-loop task.
     * A call from our own worker cannot join itself; cleanup is retried later. */
    if (running && xTaskGetCurrentTaskHandle() == worker_handle) { leave_api(); return ESP_ERR_INVALID_STATE; }
    start = now_ms();
    while (running) {
        if ((uint32_t)(now_ms() - start) >= STOP_WAIT_MS) { leave_api(); return ESP_ERR_TIMEOUT; }
        vTaskDelay(1);
        portENTER_CRITICAL(&mux); running = worker_running; portEXIT_CRITICAL(&mux);
    }
    result = cleanup(); leave_api(); return result;
}
esp_err_t esp_smartconfig_internal_start(const smartconfig_start_config_t *start)
{
    esp_err_t error, rollback;
    wifi_mode_t mode;
    wifi_country_t country;
    sc_touch2_config crypto = {0};
    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
    bool enabled;
    unsigned int first, last;
    if (start == NULL || (start->esp_touch_v2_enable_crypt && start->esp_touch_v2_key == NULL)) return ESP_ERR_INVALID_ARG;
    if (!enter_api()) return ESP_ERR_INVALID_STATE;
    if (receiver.active) { leave_api(); return ESP_ERR_INVALID_STATE; }
    error = esp_wifi_get_mode(&mode); if (error != ESP_OK) goto done;
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) { error = ESP_ERR_INVALID_STATE; goto done; }
    error = esp_wifi_get_promiscuous(&enabled); if (error != ESP_OK) goto done;
    if (enabled) { error = ESP_ERR_INVALID_STATE; goto done; }
    error = esp_wifi_get_country(&country); if (error != ESP_OK) goto done;
    first = country.schan; last = first + (unsigned int)country.nchan;
    if (first < 1) first = 1;
    if (last > 15) last = 15;
    if (country.nchan == 0 || first >= last) { error = ESP_ERR_INVALID_STATE; goto done; }
    error = esp_wifi_get_channel(&receiver.saved_channel, &receiver.saved_secondary); if (error != ESP_OK) goto done;
    error = esp_wifi_get_promiscuous_filter(&receiver.saved_filter); if (error != ESP_OK) goto done;
    receiver.active = true; receiver.logging = start->enable_log;
    receiver.first = (uint8_t)first; receiver.last = (uint8_t)(last - 1U);
    receiver.channel = receiver.first;
    if (selected_type == SC_TYPE_ESPTOUCH || selected_type == SC_TYPE_ESPTOUCH_AIRKISS) {
        receiver.v1 = sc_capture_create(); if (receiver.v1 == NULL) { error = ESP_ERR_NO_MEM; goto fail; }
    }
    if (selected_type == SC_TYPE_AIRKISS || selected_type == SC_TYPE_ESPTOUCH_AIRKISS) {
        receiver.airkiss = sc_airkiss_capture_create(); if (receiver.airkiss == NULL) { error = ESP_ERR_NO_MEM; goto fail; }
    }
    if (selected_type == SC_TYPE_ESPTOUCH_V2) {
        if (start->esp_touch_v2_enable_crypt) { memcpy(crypto.key, start->esp_touch_v2_key, 16); crypto.decrypt = sc_touch2_psa_decrypt; }
        receiver.v2 = sc_touch2_capture_create(start->esp_touch_v2_enable_crypt ? &crypto : NULL);
        if (receiver.v2 == NULL) { error = ESP_ERR_NO_MEM; goto fail; }
    }
    error = esp_wifi_set_promiscuous_filter(&filter); if (error != ESP_OK) goto fail;
    receiver.filter_changed = true;
    error = esp_wifi_set_promiscuous_rx_cb(receive_frame); if (error != ESP_OK) goto fail;
    receiver.callback = true;
    portENTER_CRITICAL(&mux); stop_requested = false; portEXIT_CRITICAL(&mux);
    error = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, discovery_event, NULL, &receiver.scan_instance);
    if (error != ESP_OK) goto fail;
    receiver.scan_handler = true;
    error = esp_event_handler_instance_register(SC_DISCOVERY_EVENT, 0, discovery_event, NULL, &receiver.fence_instance);
    if (error != ESP_OK) goto fail;
    receiver.fence_handler = true;
    report_error("disconnect", esp_wifi_disconnect());
    vTaskDelay(pdMS_TO_TICKS(50));
    begin_discovery(now_ms());
    portENTER_CRITICAL(&mux); worker_running = true; portEXIT_CRITICAL(&mux);
    if (xTaskCreate(worker, "smartconfig", 4096, NULL, 3, &worker_handle) != pdPASS) {
        portENTER_CRITICAL(&mux); worker_running = false; portEXIT_CRITICAL(&mux);
        error = ESP_ERR_NO_MEM; goto fail;
    }
    error = ESP_OK; goto done;
fail:
    rollback = cleanup(); if (rollback != ESP_OK) error = rollback;
done:
    wipe(&crypto, sizeof(crypto)); leave_api(); return error;
}
