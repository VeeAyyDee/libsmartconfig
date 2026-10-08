#ifndef SC_AIRKISS_IDF_H
#define SC_AIRKISS_IDF_H
#include "esp_err.h"
#include "esp_event.h"
#include "sc_airkiss_capture.h"
#ifdef __cplusplus
extern "C" {
#endif
ESP_EVENT_DECLARE_BASE(SC_AIRKISS_EVENT);
enum { SC_AIRKISS_FOUND_CHANNEL = 0, SC_AIRKISS_GOT_CREDENTIALS = 1 };
/* Caller owns initialization, event loop, association, and serialized polling. */
esp_err_t sc_airkiss_idf_start(void);
/* Explicit keyed mode; key has 1..16 raw bytes, copied and zero-padded. */
esp_err_t sc_airkiss_idf_start_with_key(const uint8_t *key, size_t key_len);
esp_err_t sc_airkiss_idf_poll(void);
esp_err_t sc_airkiss_idf_stop(void);
#ifdef __cplusplus
}
#endif
#endif
