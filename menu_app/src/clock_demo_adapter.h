#ifndef __CLOCK_DEMO_ADAPTER_H__
#define __CLOCK_DEMO_ADAPTER_H__

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief State of one clock-demo operation. */
typedef enum clock_demo_operation_state
{
    CLOCK_DEMO_OPERATION_IDLE = 0, /**< No operation has been requested. */
    CLOCK_DEMO_OPERATION_QUEUED,   /**< A command is waiting for the worker. */
    CLOCK_DEMO_OPERATION_RUNNING,  /**< The worker is executing the command. */
    CLOCK_DEMO_OPERATION_DONE,     /**< The command completed successfully. */
    CLOCK_DEMO_OPERATION_FAILED,   /**< The command failed. */
} clock_demo_operation_state_t;

/** @brief Thread-safe result snapshot consumed by the clock page. */
typedef struct clock_demo_adapter_snapshot
{
    clock_demo_operation_state_t sync_state; /**< SNTP command state. */
    clock_demo_operation_state_t alarm_state; /**< RTC alarm command state. */
    esp_err_t sync_result;       /**< Most recent SNTP command result. */
    esp_err_t alarm_result;      /**< Most recent RTC alarm command result. */
    esp_err_t cleanup_result;    /**< Most recent close attempt result. */
    uint32_t revision;           /**< Monotonic snapshot revision. */
    uint8_t alarm_hour;          /**< Armed UTC target hour. */
    uint8_t alarm_minute;        /**< Armed UTC target minute. */
    uint8_t alarm_second;        /**< Armed UTC target second. */
    bool sync_owned;             /**< The page started the active SNTP client. */
    bool alarm_owned;            /**< The page configured the active RTC alarm. */
    bool closing;                /**< Resource cleanup has started. */
} clock_demo_adapter_snapshot_t;

/** @brief Page-private clock worker and synchronization resources. */
typedef struct clock_demo_adapter
{
    TaskHandle_t worker;              /**< Serial time-service worker. */
    SemaphoreHandle_t lock;           /**< Protects snapshot and command state. */
    EventGroupHandle_t events;        /**< Reports cleanup attempts and exit. */
    clock_demo_adapter_snapshot_t snapshot; /**< Latest worker result. */
    bool command_pending;             /**< One foreground command is pending. */
    atomic_bool worker_tail_complete; /**< Worker no longer touches this object. */
} clock_demo_adapter_t;

/**
 * @brief Create the serial clock-demo worker.
 * @param adapter is zeroed page-private storage.
 * @return ESP_OK on success, otherwise an ESP-IDF error.
 */
esp_err_t clock_demo_adapter_open(clock_demo_adapter_t *adapter);

/**
 * @brief Queue an asynchronous SNTP synchronization request.
 * @param adapter owns the worker receiving the command.
 * @return ESP_OK when queued, otherwise an ESP-IDF error.
 */
esp_err_t clock_demo_adapter_request_sync(clock_demo_adapter_t *adapter);

/**
 * @brief Queue configuration of an RTC alarm ten seconds from now.
 * @param adapter owns the worker receiving the command.
 * @return ESP_OK when queued, otherwise an ESP-IDF error.
 */
esp_err_t clock_demo_adapter_arm_alarm(clock_demo_adapter_t *adapter);

/**
 * @brief Queue disabling of the alarm owned by this adapter.
 * @param adapter owns the worker receiving the command.
 * @return ESP_OK when queued, otherwise an ESP-IDF error.
 */
esp_err_t clock_demo_adapter_disarm_alarm(clock_demo_adapter_t *adapter);

/**
 * @brief Copy the latest worker result without touching RTC hardware.
 * @param adapter owns the snapshot.
 * @param snapshot receives a complete snapshot.
 * @return ESP_OK on success, otherwise an ESP-IDF error.
 */
esp_err_t clock_demo_adapter_get_snapshot(
    clock_demo_adapter_t *adapter,
    clock_demo_adapter_snapshot_t *snapshot);

/**
 * @brief Cancel owned SNTP/alarm work and release worker resources.
 *
 * A failed operation retains its ownership and worker resources so a later
 * lifecycle callback can retry cleanup.
 *
 * @param adapter owns all resources to close.
 * @return ESP_OK when fully closed, otherwise the retryable cleanup error.
 */
esp_err_t clock_demo_adapter_close(clock_demo_adapter_t *adapter);

/**
 * @brief Report whether worker resources are currently owned.
 * @param adapter is the page-private adapter.
 * @return true while open or cleanup remains pending.
 */
bool clock_demo_adapter_is_open(const clock_demo_adapter_t *adapter);

#ifdef __cplusplus
}
#endif

#endif /* __CLOCK_DEMO_ADAPTER_H__ */
