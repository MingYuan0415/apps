/** @file Minimal pthread-backed task API for the Flybirds task test. */

#ifndef __FLYBIRDS_FAKE_TASK_H__
#define __FLYBIRDS_FAKE_TASK_H__

#include "freertos/FreeRTOS.h"

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
                                   uint32_t stack_depth, void *argument,
                                   UBaseType_t priority, TaskHandle_t *handle,
                                   BaseType_t core_id);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);

#endif /* __FLYBIRDS_FAKE_TASK_H__ */
