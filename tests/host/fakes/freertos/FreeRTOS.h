/** @file Minimal pthread-backed FreeRTOS types for the Flybirds task test. */

#ifndef __FLYBIRDS_FAKE_FREERTOS_H__
#define __FLYBIRDS_FAKE_FREERTOS_H__

#include <stdint.h>

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef void (*TaskFunction_t)(void *);

typedef struct fake_task *TaskHandle_t;
typedef struct fake_semaphore *SemaphoreHandle_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0

#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#endif /* __FLYBIRDS_FAKE_FREERTOS_H__ */
