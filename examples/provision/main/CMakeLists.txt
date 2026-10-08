# SPDX-License-Identifier: 0BSD
get_filename_component(SC_PROVISION_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
get_filename_component(SC_PROVISION_COMPONENT_NAME "${SC_PROVISION_COMPONENT_DIR}" NAME)
idf_component_register(
    SRCS "provision_example.c"
    INCLUDE_DIRS "."
    REQUIRES "${SC_PROVISION_COMPONENT_NAME}" esp_wifi esp_event esp_netif nvs_flash lwip
)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
