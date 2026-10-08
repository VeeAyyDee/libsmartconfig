#ifndef SC_CAPTURE_H
#define SC_CAPTURE_H
#include "sc_touch.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sc_capture sc_capture;
typedef struct {
    uint8_t bssid[6], sender[6];
    uint8_t channel;
    uint16_t overhead;
} sc_capture_lock;
sc_capture *sc_capture_create(void);
void sc_capture_destroy(sc_capture *ctx);
void sc_capture_reset(sc_capture *ctx);
int sc_capture_feed(sc_capture *ctx, const uint8_t *header,
                    size_t header_bytes, uint16_t wire_length,
                    uint8_t channel, uint32_t now_ms);
int sc_capture_tick(sc_capture *ctx, uint32_t now_ms);
int sc_capture_get_state(const sc_capture *ctx);
int sc_capture_get_lock(const sc_capture *ctx, sc_capture_lock *out);
int sc_capture_get_result(const sc_capture *ctx, sc_touch_result *out);
int sc_touch_make_ack(const sc_touch_result *result,
                      const uint8_t device_mac[6], const uint8_t local_ip[4],
                      uint8_t out[11]);
#ifdef __cplusplus
}
#endif
#endif
