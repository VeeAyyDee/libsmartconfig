#ifndef SC_TOUCH2_IDF_H
#define SC_TOUCH2_IDF_H
#include "esp_err.h"
#include "esp_event.h"
#include "sc_touch2_capture.h"
#ifdef __cplusplus
extern "C" {
#endif
ESP_EVENT_DECLARE_BASE(SC_TOUCH2_EVENT);
enum { SC_TOUCH2_FOUND_CHANNEL = 0, SC_TOUCH2_GOT_CREDENTIALS = 1 };
/* Caller owns initialization, event loop, association, and serialized polling. */
esp_err_t sc_touch2_idf_start(const uint8_t *key16);
esp_err_t sc_touch2_idf_poll(void);
esp_err_t sc_touch2_idf_stop(void);
#ifdef __cplusplus
}
#endif
#endif
