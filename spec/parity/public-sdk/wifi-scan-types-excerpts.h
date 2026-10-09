/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Public SDK excerpts; dependent enum/types from complete esp_wifi_types.h. */

typedef struct {
    uint8_t *ssid;                                     /**< SSID of AP */
    uint8_t *bssid;                                    /**< MAC address of AP */
    uint8_t channel;                                   /**< Channel, scan the specific channel */
    bool show_hidden;                                  /**< Enable it to scan AP whose SSID is hidden */
    wifi_scan_type_t scan_type;                        /**< Scan type, active or passive */
    wifi_scan_time_t scan_time;                        /**< Scan time per channel */
    uint8_t home_chan_dwell_time;                      /**< Time spent at home channel between scanning consecutive channels. */
    wifi_scan_channel_bitmap_t channel_bitmap;         /**< Channel bitmap for setting specific channels to be scanned.
                                                            Please note that the 'channel' parameter above needs to be set to 0 to allow scanning by bitmap.
                                                            Also, note that only allowed channels configured by wifi_country_t can be scanned. */
    bool coex_background_scan;                         /**< Enable it to scan return home channel under coexist */
} wifi_scan_config_t;

typedef struct {
    uint32_t status;          /**< Status of scanning APs: 0 — success, 1 - failure */
    uint8_t  number;          /**< Number of scan results */
    uint8_t  scan_id;         /**< Scan sequence number, used for block scan */
} wifi_event_sta_scan_done_t;

typedef struct {
    uint8_t bssid[6];                     /**< MAC address of AP */
    uint8_t ssid[33];                     /**< SSID of AP */
    uint8_t primary;                      /**< Channel of AP */
    wifi_second_chan_t second;            /**< Secondary channel of AP */
    int8_t  rssi;                         /**< Signal strength of AP. Note that in some rare cases where signal strength is very strong, RSSI values can be slightly positive */
    wifi_auth_mode_t authmode;            /**< Auth mode of AP */
    wifi_cipher_type_t pairwise_cipher;   /**< Pairwise cipher of AP */
    wifi_cipher_type_t group_cipher;      /**< Group cipher of AP */
    wifi_ant_t ant;                       /**< Antenna used to receive beacon from AP */
    uint32_t phy_11b: 1;                  /**< Bit: 0 flag to identify if 11b mode is enabled or not */
    uint32_t phy_11g: 1;                  /**< Bit: 1 flag to identify if 11g mode is enabled or not */
    uint32_t phy_11n: 1;                  /**< Bit: 2 flag to identify if 11n mode is enabled or not */
    uint32_t phy_lr: 1;                   /**< Bit: 3 flag to identify if low rate is enabled or not */
    uint32_t phy_11a: 1;                  /**< Bit: 4 flag to identify if 11ax mode is enabled or not */
    uint32_t phy_11ac: 1;                 /**< Bit: 5 flag to identify if 11ax mode is enabled or not */
    uint32_t phy_11ax: 1;                 /**< Bit: 6 flag to identify if 11ax mode is enabled or not */
    uint32_t wps: 1;                      /**< Bit: 7 flag to identify if WPS is supported or not */
    uint32_t ftm_responder: 1;            /**< Bit: 8 flag to identify if FTM is supported in responder mode */
    uint32_t ftm_initiator: 1;            /**< Bit: 9 flag to identify if FTM is supported in initiator mode */
    uint32_t akm_dpp: 1;                  /**< Bit: 10 flag set when AP supports mixed DPP AKM (e.g., SAE + DPP or WPA2-PSK + DPP) or when AP only supports DPP AKM */
    uint32_t reserved: 21;                /**< Bit: 11..31 reserved */
    wifi_country_t country;               /**< Country information of AP */
    wifi_he_ap_info_t he_ap;              /**< HE AP info */
    wifi_bandwidth_t bandwidth;           /**< Bandwidth of AP */
    uint8_t vht_ch_freq1;                 /**< This fields are used only AP bandwidth is 80 and 160 MHz, to transmit the center channel
                                               frequency of the BSS. For AP bandwidth is 80 + 80 MHz, it is the center channel frequency
                                               of the lower frequency segment.*/
    uint8_t vht_ch_freq2;                 /**< this fields are used only AP bandwidth is 80 + 80 MHz, and is used to transmit the center
                                               channel frequency of the second segment. */
} wifi_ap_record_t;
