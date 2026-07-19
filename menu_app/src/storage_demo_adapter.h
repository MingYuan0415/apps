#ifndef __STORAGE_DEMO_ADAPTER_H__
#define __STORAGE_DEMO_ADAPTER_H__

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_DEMO_PAGE_ID "storage"

/** @brief Storage operation currently executing on the adapter worker. */
typedef enum
{
    STORAGE_DEMO_OPERATION_NONE = 0, /**< No operation is active. */
    STORAGE_DEMO_OPERATION_REFRESH,  /**< Mount and capacity query is active. */
    STORAGE_DEMO_OPERATION_SELF_TEST, /**< The 4 KiB self-test is active. */
} storage_demo_operation_t;

/** @brief Result of the most recent explicit storage self-test. */
typedef enum
{
    STORAGE_DEMO_SELF_TEST_NOT_RUN = 0, /**< No self-test has completed. */
    STORAGE_DEMO_SELF_TEST_RUNNING,     /**< A self-test is active. */
    STORAGE_DEMO_SELF_TEST_PASSED,      /**< Write and read-back matched. */
    STORAGE_DEMO_SELF_TEST_FAILED,      /**< The self-test did not complete. */
} storage_demo_self_test_result_t;

/** @brief Thread-safe storage state copied for the UI worker. */
typedef struct storage_demo_snapshot
{
    storage_demo_operation_t operation; /**< Current worker operation. */
    storage_demo_self_test_result_t self_test; /**< Latest self-test state. */
    uint64_t total_bytes;     /**< Filesystem capacity when valid. */
    uint64_t free_bytes;      /**< Filesystem free space when valid. */
    uint32_t generation;      /**< Monotonic snapshot generation. */
    uint32_t self_test_count; /**< Number of successful self-tests. */
    esp_err_t last_error;     /**< Latest service or filesystem error. */
    int filesystem_errno;     /**< POSIX errno for the latest file failure. */
    bool ready;               /**< Initial mount query has completed. */
    bool mounted;             /**< The SD service reports a mounted card. */
    bool capacity_valid;      /**< Capacity fields contain current values. */
    bool accepting_commands;  /**< New refresh and self-test work is allowed. */
} storage_demo_snapshot_t;

/** @brief Opaque page-owned worker adapter. */
typedef struct storage_demo_adapter storage_demo_adapter_t;

/**
 * @brief Start one page-owned storage worker and queue an initial refresh.
 *
 * @param adapter receives the new adapter and must initially point to NULL.
 *
 * @return ESP_OK when the worker is running, otherwise an ESP-IDF error.
 */
esp_err_t storage_demo_adapter_open(storage_demo_adapter_t **adapter);

/**
 * @brief Queue a non-blocking mount and capacity refresh.
 * @param adapter owns a running worker.
 * @return ESP_OK when queued, ESP_ERR_INVALID_STATE while another operation
 *         or teardown is active, otherwise an ESP-IDF error.
 */
esp_err_t storage_demo_adapter_refresh(storage_demo_adapter_t *adapter);

/**
 * @brief Queue the explicit 4 KiB temporary-file self-test.
 * @param adapter owns a running worker.
 * @return ESP_OK when queued, ESP_ERR_INVALID_STATE while another operation
 *         or teardown is active, otherwise an ESP-IDF error.
 */
esp_err_t storage_demo_adapter_run_self_test(
    storage_demo_adapter_t *adapter);

/**
 * @brief Copy the latest worker snapshot without performing filesystem I/O.
 * @param adapter owns a running worker.
 * @param snapshot receives a consistent copy.
 * @return ESP_OK when copied, otherwise an ESP-IDF error.
 */
esp_err_t storage_demo_adapter_get_snapshot(
    storage_demo_adapter_t *adapter,
    storage_demo_snapshot_t *snapshot);

/**
 * @brief Cancel active work, remove an owned temporary file, and stop.
 *
 * @note On cleanup failure or timeout, the adapter remains owned through the
 *       caller's pointer so a later close can retry the exact cleanup.
 *
 * @param adapter points to the page-owned adapter and is cleared on success.
 * @return ESP_OK after complete teardown, otherwise a retryable error.
 */
esp_err_t storage_demo_adapter_close(storage_demo_adapter_t **adapter);

#ifdef __cplusplus
}
#endif

#endif /* __STORAGE_DEMO_ADAPTER_H__ */
