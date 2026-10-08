# SPDX-License-Identifier: 0BSD
idf_component_register(
    SRCS
        "src/sc_touch.c"
        "src/sc_capture.c"
        "src/sc_airkiss.c"
        "src/sc_airkiss_capture.c"
        "src/sc_touch2.c"
        "src/sc_touch2_capture.c"
        "idf/sc_touch_idf.c"
        "idf/sc_airkiss_idf.c"
        "idf/sc_touch2_idf.c"
        "idf/sc_touch2_psa.c"
    INCLUDE_DIRS "include" "idf"
    REQUIRES esp_wifi esp_event esp_timer mbedtls
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
