#ifndef SC_TOUCH_IDF_H
#define SC_TOUCH_IDF_H
#include "esp_err.h"
#include "esp_event.h"
#include "sc_capture.h"
#ifdef __cplusplus
extern "C" {
#endif
ESP_EVENT_DECLARE_BASE(SC_TOUCH_EVENT);
enum { SC_TOUCH_FOUND_CHANNEL = 0, SC_TOUCH_GOT_CREDENTIALS = 1 };
/* Caller owns initialization, event loop, association, and serialized polling. */
esp_err_t sc_touch_idf_start(void);
esp_err_t sc_touch_idf_poll(void);
esp_err_t sc_touch_idf_stop(void);
#ifdef __cplusplus
}
#endif
#endif
