/* SPDX-License-Identifier: 0BSD */
#include "sdkconfig.h"
#include "sc_touch_idf.h"
#include "sc_touch2_idf.h"
#include "sc_airkiss_idf.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#if CONFIG_SC_EXAMPLE_PROTOCOL_V1
typedef sc_touch_result decoded_result;
#define SELECTED_EVENT SC_TOUCH_EVENT
#define SELECTED_FOUND SC_TOUCH_FOUND_CHANNEL
#define SELECTED_CREDENTIALS SC_TOUCH_GOT_CREDENTIALS
static esp_err_t adapter_start(const uint8_t *key) { (void)key; return sc_touch_idf_start(); }
static esp_err_t adapter_poll(void) { return sc_touch_idf_poll(); }
static esp_err_t adapter_stop(void) { return sc_touch_idf_stop(); }
#elif CONFIG_SC_EXAMPLE_PROTOCOL_AIRKISS
typedef sc_airkiss_result decoded_result;
#define SELECTED_EVENT SC_AIRKISS_EVENT
#define SELECTED_FOUND SC_AIRKISS_FOUND_CHANNEL
#define SELECTED_CREDENTIALS SC_AIRKISS_GOT_CREDENTIALS
static esp_err_t adapter_start(const uint8_t *key)
{ return key != NULL ? sc_airkiss_idf_start_with_key(key, 16) : sc_airkiss_idf_start(); }
static esp_err_t adapter_poll(void) { return sc_airkiss_idf_poll(); }
static esp_err_t adapter_stop(void) { return sc_airkiss_idf_stop(); }
#else
typedef sc_touch2_result decoded_result;
#define SELECTED_EVENT SC_TOUCH2_EVENT
#define SELECTED_FOUND SC_TOUCH2_FOUND_CHANNEL
#define SELECTED_CREDENTIALS SC_TOUCH2_GOT_CREDENTIALS
static esp_err_t adapter_start(const uint8_t *key) { return sc_touch2_idf_start(key); }
static esp_err_t adapter_poll(void) { return sc_touch2_idf_poll(); }
static esp_err_t adapter_stop(void) { return sc_touch2_idf_stop(); }
#endif

enum { QUEUE_LENGTH = 12, PROVISION_SECONDS = 120, ASSOCIATION_SECONDS = 30,
       ACK_SENDS = 30, MAX_CONNECT_ATTEMPTS = 3 };
typedef enum { APP_WIFI_STARTED, APP_DISCONNECTED, APP_GOT_IP,
               APP_CAPTURE_LOCK, APP_CREDENTIALS } app_event_kind;
typedef struct {
    app_event_kind kind;
    union {
        sc_capture_lock lock;
        decoded_result credentials;
        esp_ip4_addr_t ip;
        uint16_t disconnect_reason;
    } data;
} app_event;
typedef struct {
    QueueHandle_t queue;
    atomic_bool accepting;
    atomic_bool overflow;
} event_context;
typedef struct {
    sc_capture_lock lock;
    decoded_result credentials;
    bool have_lock, have_credentials;
} provisioning_session;

static const char *TAG = "sc_example";
/* Static storage remains valid even if a late callback follows cleanup. */
static StaticQueue_t queue_control;
static uint8_t queue_storage[QUEUE_LENGTH * sizeof(app_event)];
static event_context events;

static void delay_ms(uint32_t milliseconds)
{
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    vTaskDelay(ticks != 0 ? ticks : 1);
}
static bool checked(esp_err_t error, const char *operation)
{
    if (error == ESP_OK) return true;
    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
    return false;
}
static bool queue_healthy(void)
{
    if (!atomic_exchange(&events.overflow, false)) return true;
    ESP_LOGE(TAG, "Event queue lost a required event; aborting this session");
    return false;
}

/* The event-loop task only copies bounded data and records queue failures. */
static void copy_event(void *argument, esp_event_base_t base, int32_t id, void *data)
{
    event_context *context = argument;
    app_event event = {0};
    if (!atomic_load(&context->accepting)) return;
    if (base == SELECTED_EVENT && id == SELECTED_FOUND && data != NULL) {
        event.kind = APP_CAPTURE_LOCK;
        memcpy(&event.data.lock, data, sizeof(event.data.lock));
    } else if (base == SELECTED_EVENT && id == SELECTED_CREDENTIALS && data != NULL) {
        event.kind = APP_CREDENTIALS;
        memcpy(&event.data.credentials, data, sizeof(event.data.credentials));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        event.kind = APP_WIFI_STARTED;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && data != NULL) {
        const wifi_event_sta_disconnected_t *disconnected = data;
        event.kind = APP_DISCONNECTED;
        event.data.disconnect_reason = disconnected->reason;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && data != NULL) {
        const ip_event_got_ip_t *got_ip = data;
        event.kind = APP_GOT_IP;
        event.data.ip = got_ip->ip_info.ip;
    } else return;
    if (xQueueSend(context->queue, &event, 0) != pdTRUE)
        atomic_store(&context->overflow, true);
}

#if CONFIG_SC_EXAMPLE_PROTOCOL_V2 || CONFIG_SC_EXAMPLE_PROTOCOL_AIRKISS
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
#endif
static bool configured_key(uint8_t key[16], bool *present)
{
    *present = false;
    memset(key, 0, 16);
#if CONFIG_SC_EXAMPLE_PROTOCOL_V2 || CONFIG_SC_EXAMPLE_PROTOCOL_AIRKISS
#if CONFIG_SC_EXAMPLE_PROTOCOL_AIRKISS
    const char *hex = CONFIG_SC_EXAMPLE_AIRKISS_KEY;
#else
    const char *hex = CONFIG_SC_EXAMPLE_V2_KEY;
#endif
    size_t length = strlen(hex), i;
    if (length == 0) return true;
    if (length != 32) {
        ESP_LOGE(TAG, "Configured protocol key must contain exactly 32 hexadecimal digits");
        return false;
    }
    for (i = 0; i < 16; ++i) {
        int high = hex_digit(hex[2 * i]), low = hex_digit(hex[2 * i + 1]);
        if (high < 0 || low < 0) {
            memset(key, 0, 16);
            ESP_LOGE(TAG, "Configured protocol key contains a non-hexadecimal character");
            return false;
        }
        key[i] = (uint8_t)((high << 4) | low);
    }
    *present = true;
#endif
    return true;
}

static bool stop_with_retry(void)
{
    unsigned int attempt;
    for (attempt = 0; attempt < 5; ++attempt) {
        esp_err_t error = adapter_stop();
        if (error == ESP_OK) return true;
        ESP_LOGW(TAG, "Capture cleanup attempt %u failed: %s", attempt + 1, esp_err_to_name(error));
        delay_ms(50);
    }
    ESP_LOGE(TAG, "Capture cleanup did not finish; association will not start");
    return false;
}
static bool await_wifi_start(void)
{
    int64_t deadline = esp_timer_get_time() + INT64_C(5000000);
    while (esp_timer_get_time() < deadline) {
        app_event event;
        if (!queue_healthy()) return false;
        if (xQueueReceive(events.queue, &event, pdMS_TO_TICKS(10)) == pdTRUE &&
            event.kind == APP_WIFI_STARTED) return true;
    }
    ESP_LOGE(TAG, "Wi-Fi start event timed out");
    return false;
}
static bool provision(provisioning_session *session, const uint8_t *key)
{
    esp_err_t error = adapter_start(key);
    int64_t deadline = esp_timer_get_time() + INT64_C(1000000) * PROVISION_SECONDS;
    bool success = false, post_wait_reported = false;
    if (error != ESP_OK) {
        (void)checked(error, "Start provisioning");
        (void)stop_with_retry(); /* Start may have left retryable rollback state. */
        return false;
    }
    ESP_LOGI(TAG, "Waiting for provisioning data (maximum %u seconds)", PROVISION_SECONDS);
    while (esp_timer_get_time() < deadline) {
        unsigned int work;
        error = adapter_poll();
        if (error == ESP_ERR_TIMEOUT || error == ESP_ERR_NO_MEM) {
            if (!post_wait_reported) ESP_LOGW(TAG, "Provisioning event delivery is pending; retrying");
            post_wait_reported = true;
        } else if (!checked(error, "Poll provisioning")) break;
        else post_wait_reported = false;
        if (!queue_healthy()) break;
        for (work = 0; work < QUEUE_LENGTH; ++work) {
            app_event event;
            if (xQueueReceive(events.queue, &event, 0) != pdTRUE) break;
            if (event.kind == APP_CAPTURE_LOCK) {
                session->lock = event.data.lock;
                session->have_lock = true;
                ESP_LOGI(TAG, "Capture locked; waiting for complete credentials");
            } else if (event.kind == APP_CREDENTIALS) {
                session->credentials = event.data.credentials;
                session->have_credentials = true;
            }
        }
        if (session->have_credentials) {
            success = session->have_lock;
            if (!success) ESP_LOGE(TAG, "Credentials arrived without a capture lock event");
            break;
        }
        delay_ms(10);
    }
    if (!session->have_credentials) ESP_LOGE(TAG, "Provisioning ended without complete credentials");
    /* Always stop before association; never force association to lock.channel. */
    if (!stop_with_retry()) return false;
    return success;
}
static bool associate(const provisioning_session *session, esp_ip4_addr_t *local_ip)
{
    const decoded_result *credentials = &session->credentials;
    wifi_config_t config = {0};
    unsigned int attempts = 1;
    int64_t deadline;
    app_event event;
#if CONFIG_SC_EXAMPLE_PROTOCOL_V2
    if (!credentials->ipv4) {
        ESP_LOGE(TAG, "IPv6-marked v2 messages are unsupported by this IPv4-only example");
        return false;
    }
#endif
    if (credentials->ssid_len == 0 || credentials->ssid_len > sizeof(config.sta.ssid) ||
        credentials->password_len > sizeof(config.sta.password)) {
        ESP_LOGE(TAG, "Decoded credential lengths cannot be used by this Wi-Fi example");
        return false;
    }
    if (memchr(credentials->ssid, 0, credentials->ssid_len) != NULL ||
        memchr(credentials->password, 0, credentials->password_len) != NULL) {
        ESP_LOGE(TAG, "This Wi-Fi example does not accept embedded NUL credential bytes");
        return false;
    }
    memcpy(config.sta.ssid, credentials->ssid, credentials->ssid_len);
    memcpy(config.sta.password, credentials->password, credentials->password_len);
    config.sta.bssid_set = true;
#if CONFIG_SC_EXAMPLE_PROTOCOL_V1
    memcpy(config.sta.bssid, credentials->bssid, sizeof(config.sta.bssid));
#else
    memcpy(config.sta.bssid, session->lock.bssid, sizeof(config.sta.bssid));
#endif
    /* Leave channel zero: captured channel can differ from the AP primary. */
    if (!checked(esp_wifi_set_config(WIFI_IF_STA, &config), "Set RAM Wi-Fi configuration")) return false;
    for (unsigned int work = 0; work < QUEUE_LENGTH; ++work)
        if (xQueueReceive(events.queue, &event, 0) != pdTRUE) break;
    if (!queue_healthy() || !checked(esp_wifi_connect(), "Begin association")) return false;
    deadline = esp_timer_get_time() + INT64_C(1000000) * ASSOCIATION_SECONDS;
    ESP_LOGI(TAG, "Associating and waiting for DHCP");
    while (esp_timer_get_time() < deadline) {
        if (!queue_healthy()) return false;
        if (xQueueReceive(events.queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        if (event.kind == APP_GOT_IP && event.data.ip.addr != 0) {
            *local_ip = event.data.ip;
            ESP_LOGI(TAG, "DHCP address: " IPSTR, IP2STR(local_ip));
            return true;
        }
        if (event.kind == APP_DISCONNECTED) {
            if (attempts >= MAX_CONNECT_ATTEMPTS) {
                ESP_LOGE(TAG, "Association failed after %u attempts (reason %u)", attempts,
                         (unsigned int)event.data.disconnect_reason);
                return false;
            }
            ++attempts;
            ESP_LOGW(TAG, "Retrying association (%u/%u)", attempts, MAX_CONNECT_ATTEMPTS);
            delay_ms(250);
            if (!checked(esp_wifi_connect(), "Retry association")) return false;
        }
    }
    ESP_LOGE(TAG, "Association/DHCP timed out");
    return false;
}

static bool send_acknowledgments(const provisioning_session *session, const esp_ip4_addr_t *local_ip)
{
    uint8_t ack[11], device_mac[6];
    struct sockaddr_in destination = {0};
    uint16_t port;
    size_t length;
    int socket_fd, flags;
    unsigned int attempt, accepted = 0;
    bool success = true;
    if (!checked(esp_wifi_get_mac(WIFI_IF_STA, device_mac), "Read device MAC")) return false;
    destination.sin_family = AF_INET;
#if CONFIG_SC_EXAMPLE_PROTOCOL_V1
    uint8_t ip_bytes[4];
    /* esp_ip4_addr_t.addr is already stored in network byte order. */
    memcpy(ip_bytes, &local_ip->addr, sizeof(ip_bytes));
    if (!sc_touch_make_ack(&session->credentials, device_mac, ip_bytes, ack)) return false;
    memcpy(&destination.sin_addr.s_addr, session->credentials.sender_ip, 4);
    port = 18266; length = 11;
#elif CONFIG_SC_EXAMPLE_PROTOCOL_AIRKISS
    (void)local_ip;
    if (!sc_airkiss_make_ack(&session->credentials, ack)) return false;
    destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    port = 10000; length = 1;
#else
    (void)local_ip;
    if (!sc_touch2_make_ack(&session->credentials, device_mac, ack, &port)) return false;
    destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    length = 7;
#endif
    destination.sin_port = htons(port);
    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) { ESP_LOGE(TAG, "UDP socket failed (errno %d)", errno); return false; }
    flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ESP_LOGE(TAG, "Nonblocking UDP setup failed (errno %d)", errno); success = false;
    }
#if !CONFIG_SC_EXAMPLE_PROTOCOL_V1
    if (success) {
        int enabled = 1;
        if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) < 0) {
            ESP_LOGE(TAG, "UDP broadcast setup failed (errno %d)", errno); success = false;
        }
    }
#endif
    for (attempt = 0; success && attempt < ACK_SENDS; ++attempt) {
        app_event event;
        if (!queue_healthy()) { success = false; break; }
        for (unsigned int work = 0; work < QUEUE_LENGTH; ++work) {
            if (xQueueReceive(events.queue, &event, 0) != pdTRUE) break;
            if (event.kind == APP_DISCONNECTED) {
                ESP_LOGE(TAG, "Wi-Fi disconnected before acknowledgment sends finished");
                success = false;
            }
        }
        if (!success) break;
        ssize_t sent = sendto(socket_fd, ack, length, 0, (struct sockaddr *)&destination, sizeof(destination));
        if (sent == (ssize_t)length) ++accepted;
        else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOMEM || errno == ENOBUFS))
            ESP_LOGW(TAG, "UDP send temporarily unavailable; retrying within the send budget");
        else { ESP_LOGE(TAG, "UDP acknowledgment send failed (errno %d)", errno); success = false; }
        if (attempt + 1 < ACK_SENDS) delay_ms(100);
    }
    if (close(socket_fd) < 0) { ESP_LOGE(TAG, "UDP close failed (errno %d)", errno); success = false; }
    if (accepted == 0) success = false;
    if (success) ESP_LOGI(TAG, "%u acknowledgment datagrams accepted locally; phone receipt is not confirmed", accepted);
    return success;
}

void app_main(void)
{
    uint8_t key[16]; bool have_key, wifi_initialized = false, wifi_started = false;
    bool default_loop_created = false, provision_registered = false, wifi_registered = false;
    bool ip_registered = false, success = false;
    provisioning_session session = {0};
    esp_ip4_addr_t local_ip = {0};
    esp_netif_t *station = NULL;
    esp_event_handler_instance_t provision_handler, wifi_handler, ip_handler;
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t error;
    if (!configured_key(key, &have_key)) return;
    /* SDK association info messages may contain the SSID. */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    events.queue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(app_event), queue_storage, &queue_control);
    atomic_init(&events.accepting, true); atomic_init(&events.overflow, false);
    if (events.queue == NULL) { ESP_LOGE(TAG, "Event queue creation failed"); goto cleanup; }
    error = nvs_flash_init();
    if (!checked(error, "Initialize NVS (no automatic erase)")) goto cleanup;
    if (!checked(esp_netif_init(), "Initialize network interfaces")) goto cleanup;
    if (!checked(esp_event_loop_create_default(), "Create default event loop")) goto cleanup;
    default_loop_created = true;
    station = esp_netif_create_default_wifi_sta();
    if (station == NULL) { ESP_LOGE(TAG, "STA network interface creation failed"); goto cleanup; }
    if (!checked(esp_wifi_init(&wifi_init), "Initialize Wi-Fi")) goto cleanup;
    wifi_initialized = true;
    if (!checked(esp_wifi_set_storage(WIFI_STORAGE_RAM), "Select RAM Wi-Fi configuration") ||
        !checked(esp_wifi_set_mode(WIFI_MODE_STA), "Select STA mode")) goto cleanup;
    if (!checked(esp_event_handler_instance_register(SELECTED_EVENT, ESP_EVENT_ANY_ID, copy_event,
                                                    &events, &provision_handler), "Register provisioning events")) goto cleanup;
    provision_registered = true;
    if (!checked(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, copy_event,
                                                    &events, &wifi_handler), "Register Wi-Fi events")) goto cleanup;
    wifi_registered = true;
    if (!checked(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, copy_event,
                                                    &events, &ip_handler), "Register IP events")) goto cleanup;
    ip_registered = true;
    if (!checked(esp_wifi_start(), "Start Wi-Fi")) goto cleanup;
    wifi_started = true;
    if (!await_wifi_start() || !provision(&session, have_key ? key : NULL)) goto cleanup;
    memset(key, 0, sizeof(key));
    if (!associate(&session, &local_ip) || !send_acknowledgments(&session, &local_ip)) goto cleanup;
    success = true;
cleanup:
    memset(key, 0, sizeof(key));
    atomic_store(&events.accepting, false);
    if (provision_registered) (void)checked(esp_event_handler_instance_unregister(SELECTED_EVENT, ESP_EVENT_ANY_ID,
                                                                              provision_handler), "Unregister provisioning events");
    if (wifi_registered) (void)checked(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                                         wifi_handler), "Unregister Wi-Fi events");
    if (ip_registered) (void)checked(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                                       ip_handler), "Unregister IP events");
    if (!success) {
        if (wifi_started) {
            error = esp_wifi_disconnect();
            if (error != ESP_ERR_WIFI_NOT_CONNECT) (void)checked(error, "Disconnect Wi-Fi during cleanup");
            (void)checked(esp_wifi_stop(), "Stop Wi-Fi during cleanup");
        }
        if (wifi_initialized) (void)checked(esp_wifi_deinit(), "Deinitialize Wi-Fi during cleanup");
        if (station != NULL) esp_netif_destroy_default_wifi(station);
        if (default_loop_created) (void)checked(esp_event_loop_delete_default(), "Delete event loop during cleanup");
        ESP_LOGE(TAG, "Provisioning example ended without completion; reboot to try again");
    } else ESP_LOGI(TAG, "Provisioning example complete; station remains connected");
    memset(&session, 0, sizeof(session));
}
