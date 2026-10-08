#ifndef SC_AIRKISS_CAPTURE_H
#define SC_AIRKISS_CAPTURE_H
#include "sc_capture.h"
#include "sc_airkiss.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sc_airkiss_capture sc_airkiss_capture;

sc_airkiss_capture *sc_airkiss_capture_create(void);
void sc_airkiss_capture_destroy(sc_airkiss_capture *ctx);
void sc_airkiss_capture_reset(sc_airkiss_capture *ctx);
int sc_airkiss_capture_feed(sc_airkiss_capture *ctx, const uint8_t *header,
                    size_t header_bytes, uint16_t wire_length,
                    uint8_t channel, uint32_t now_ms);
int sc_airkiss_capture_tick(sc_airkiss_capture *ctx, uint32_t now_ms);
int sc_airkiss_capture_get_state(const sc_airkiss_capture *ctx);
int sc_airkiss_capture_get_lock(const sc_airkiss_capture *ctx, sc_capture_lock *out);
int sc_airkiss_capture_get_result(const sc_airkiss_capture *ctx, sc_airkiss_result *out);
#ifdef __cplusplus
}
#endif
#endif
