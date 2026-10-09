#ifndef TEST_FREERTOS_TASK_H
#define TEST_FREERTOS_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
int xTaskCreate(TaskFunction_t function, const char *name, unsigned int size,
                void *arg, unsigned int priority, TaskHandle_t *handle);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskDelay(unsigned int ticks);
void vTaskDelete(TaskHandle_t handle);
#endif
