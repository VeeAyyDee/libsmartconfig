#ifndef TEST_ESP_EVENT_H
#define TEST_ESP_EVENT_H
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg, esp_event_handler_instance_t *instance);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id, esp_event_handler_instance_t instance);
#define ESP_EVENT_DECLARE_BASE(name) extern esp_event_base_t name
#define ESP_EVENT_DEFINE_BASE(name) esp_event_base_t name = #name
esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void *data,
                          size_t bytes, unsigned int wait);
#endif
