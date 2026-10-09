# SPDX-License-Identifier: 0BSD
get_filename_component(LIBSMARTCONFIG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
get_filename_component(LIBSMARTCONFIG_COMPONENT "${LIBSMARTCONFIG_ROOT}" NAME)
idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
    REQUIRES ${LIBSMARTCONFIG_COMPONENT} esp_wifi esp_event esp_netif nvs_flash)
