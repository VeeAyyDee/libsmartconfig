#ifndef SC_TOUCH2_CAPTURE_H
#define SC_TOUCH2_CAPTURE_H
#include "sc_capture.h"
#include "sc_touch2.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sc_touch2_capture sc_touch2_capture;

sc_touch2_capture *sc_touch2_capture_create(const sc_touch2_config *config);
void sc_touch2_capture_destroy(sc_touch2_capture *ctx);
void sc_touch2_capture_reset(sc_touch2_capture *ctx);
int sc_touch2_capture_feed(sc_touch2_capture *ctx, const uint8_t *header,
                    size_t header_bytes, uint16_t wire_length,
                    uint8_t channel, uint32_t now_ms);
int sc_touch2_capture_tick(sc_touch2_capture *ctx, uint32_t now_ms);
int sc_touch2_capture_get_state(const sc_touch2_capture *ctx);
int sc_touch2_capture_get_lock(const sc_touch2_capture *ctx, sc_capture_lock *out);
int sc_touch2_capture_get_result(const sc_touch2_capture *ctx, sc_touch2_result *out);
#ifdef __cplusplus
}
#endif
#endif
