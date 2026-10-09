#ifndef TEST_ESP_WIFI_H
#define TEST_ESP_WIFI_H
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA = 1, WIFI_MODE_APSTA = 3 } wifi_mode_t;
typedef enum { WIFI_SECOND_CHAN_NONE = 0, WIFI_SECOND_CHAN_ABOVE = 1 } wifi_second_chan_t;
typedef enum { WIFI_PKT_MGMT = 0, WIFI_PKT_DATA = 1 } wifi_promiscuous_pkt_type_t;
typedef struct { unsigned int filter_mask; } wifi_promiscuous_filter_t;
typedef struct { uint8_t schan, nchan; } wifi_country_t;
typedef struct { int unused; } wifi_ap_record_t;
typedef struct { uint16_t sig_len; uint8_t channel, rx_state; } wifi_pkt_rx_ctrl_t;
typedef struct { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t payload[26]; } wifi_promiscuous_pkt_t;
typedef void (*wifi_promiscuous_cb_t)(void *, wifi_promiscuous_pkt_type_t);
#define WIFI_PROMIS_FILTER_MASK_DATA 2U
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *record);
esp_err_t esp_wifi_get_promiscuous(bool *enabled);
esp_err_t esp_wifi_get_country(wifi_country_t *country);
esp_err_t esp_wifi_get_channel(uint8_t *channel, wifi_second_chan_t *secondary);
esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *filter);
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter);
esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t callback);
esp_err_t esp_wifi_set_channel(uint8_t channel, wifi_second_chan_t secondary);
esp_err_t esp_wifi_set_promiscuous(bool enabled);
#endif
