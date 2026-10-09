/* SPDX-License-Identifier: 0BSD */
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "libsmartconfig";
static EventGroupHandle_t status;
enum { SESSION_DONE = 1, SESSION_FAILED = 2, WIFI_READY = 4 };
static bool connecting;
static unsigned int connect_attempts;
static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
static void failure(const char *operation, esp_err_t error)
{
    ESP_LOGE(TAG, "%s: %s", operation, esp_err_to_name(error));
    xEventGroupSetBits(status, SESSION_FAILED);
}
static void event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(status, WIFI_READY);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && connecting) {
        if (++connect_attempts < 3) {
            esp_err_t error = esp_wifi_connect();
            if (error != ESP_OK) failure("Retry association", error);
        } else failure("Association attempts exhausted", ESP_FAIL);
    } else if (base == SC_EVENT && id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Provisioning channel found");
    } else if (base == SC_EVENT && id == SC_EVENT_GOT_SSID_PSWD) {
        const smartconfig_event_got_ssid_pswd_t *result = data;
        wifi_config_t config = {0};
        esp_err_t error;
        memcpy(config.sta.ssid, result->ssid, sizeof(config.sta.ssid));
        memcpy(config.sta.password, result->password, sizeof(config.sta.password));
        config.sta.bssid_set = result->bssid_set;
        memcpy(config.sta.bssid, result->bssid, sizeof(config.sta.bssid));
        /* Do not force a capture channel; SDK association chooses AP primary. */
        error = esp_wifi_set_config(WIFI_IF_STA, &config);
        memset(&config, 0, sizeof(config));
        if (error != ESP_OK) { failure("Apply RAM credentials", error); return; }
        connecting = true; connect_attempts = 0;
        error = esp_wifi_connect();
        if (error != ESP_OK) failure("Associate", error);
    } else if (base == SC_EVENT && id == SC_EVENT_SEND_ACK_DONE) {
        esp_err_t error = esp_smartconfig_stop();
        if (error != ESP_OK) failure("Stop provisioning", error);
        else xEventGroupSetBits(status, SESSION_DONE);
    }
}
void app_main(void)
{
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    smartconfig_start_config_t config = SMARTCONFIG_START_CONFIG_DEFAULT();
    const char *encoded = CONFIG_LIBSMARTCONFIG_EXAMPLE_V2_KEY;
    uint8_t key[16] = {0};
    esp_err_t error;
    EventBits_t bits;
    size_t i;
    esp_log_level_set("wifi", ESP_LOG_WARN);
    if (encoded[0] != 0) {
        if (CONFIG_LIBSMARTCONFIG_EXAMPLE_TYPE != 3 || strlen(encoded) != 32) {
            ESP_LOGE(TAG, "Key requires v2 and exactly 32 hexadecimal digits"); return;
        }
        for (i = 0; i < 16; ++i) {
            int a = hex_digit(encoded[2 * i]), b = hex_digit(encoded[2 * i + 1]);
            if (a < 0 || b < 0) { ESP_LOGE(TAG, "Key contains a non-hexadecimal digit"); return; }
            key[i] = (uint8_t)((a << 4) | b);
        }
        config.esp_touch_v2_enable_crypt = true; config.esp_touch_v2_key = (char *)key;
    }
    error = nvs_flash_init();
    if (error != ESP_OK) { ESP_LOGE(TAG, "NVS init failed; storage was not erased"); return; }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (esp_netif_create_default_wifi_sta() == NULL) return;
    status = xEventGroupCreate(); if (status == NULL) return;
    ESP_ERROR_CHECK(esp_wifi_init(&wifi));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());
    if ((xEventGroupWaitBits(status, WIFI_READY, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000)) & WIFI_READY) == 0) {
        failure("Wi-Fi start deadline", ESP_ERR_TIMEOUT); return;
    }
    ESP_ERROR_CHECK(esp_smartconfig_set_type((smartconfig_type_t)CONFIG_LIBSMARTCONFIG_EXAMPLE_TYPE));
    error = esp_smartconfig_start(&config);
    memset(key, 0, sizeof(key));
    if (error != ESP_OK) { failure("Start provisioning", error); return; }
    bits = xEventGroupWaitBits(status, SESSION_DONE | SESSION_FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(180000));
    if ((bits & SESSION_DONE) == 0) {
        for (i = 0; i < 5; ++i) {
            error = esp_smartconfig_stop();
            if (error == ESP_OK) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    if ((bits & SESSION_DONE) != 0) ESP_LOGI(TAG, "SDK acknowledgment workflow completed");
    else ESP_LOGE(TAG, "Provisioning failed or deadline reached");
    /* One session per boot. Keep event state alive for any queued SDK events. */
}
